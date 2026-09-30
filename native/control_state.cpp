// 常驻探索输入协调器：原生输入先执行，只有安全空闲帧才提交设置或加入角色。
// 渲染线程只读快照并提交期望值；不同线程之间不共享可解引用的游戏对象指针。
#include "control_state.h"
#include "feature_state.h"
#include "runtime.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <mutex>

namespace sky2party {
namespace {
using FieldInput = bool(*)(uintptr_t, uint32_t);
FieldInput originalFieldInput = nullptr;
std::atomic<uint32_t> requested{kDefaultFeatures}, applied{0};
std::atomic<uint64_t> revision{1}, attemptedRevision{0};
std::atomic<DWORD> gameThread{0};
std::atomic<bool> stateKnown{true};
std::atomic<int32_t> moduleActivity{1};
std::atomic<ControlMessage> activityMessage{ControlMessage::None};
std::mutex messageLock;
ControlMessage status = ControlMessage::None;
FixedMemberGuardResult lastFixedGuard{};
uint64_t fixedGuardCapturedAt = 0;

void SetStatus(ControlMessage message) noexcept {
    std::lock_guard<std::mutex> guard(messageLock);
    status = message;
}

bool MatchTickSignature(uintptr_t address) noexcept {
    const unsigned char expected[]{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,
        0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90,0x00,
        0x00,0x00,0x0F,0x29,0x70,0xC8};
    __try { return std::memcmp(reinterpret_cast<const void*>(address), expected, sizeof(expected)) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ProcessActivityRequest(int32_t activity,bool safe,const FixedMemberGuardResult& fixedGuard,
    uintptr_t context,uint32_t actionMask,bool handled) noexcept {
    if(activity!=2&&activity!=3)return false;
    // 请求不会从 Present 等待游戏；处于菜单/过场时保持“启用中/停用中”，
    // 直到普通探索回调明确给出安全许可。队列若正在原生调用内，也只等待收尾。
    if(!safe||!RosterServiceIdle())return true;
    const bool enabling=activity==2;
    const uint32_t before=applied.load();
    const uint32_t wanted=enabling?(requested.load()&kAllFeatures):0;
    const auto fail=[&](ControlMessage reason){
        activityMessage.store(reason);
        // 启用失败只有在确证回滚到原字节时才可回到“停用”。若部分写入/缓存
        // 刷新失败使状态未知，保守保留活动记录并报告错误，不能把残留效果隐去。
        const int32_t stable=enabling&&stateKnown.load()?0:1;
        if(stable==1)SetRosterRequestsEnabled(true);
        moduleActivity.store(stable,std::memory_order_release);
    };
    if(!stateKnown.load()){fail(ControlMessage::ApplyFailed);return true;}
    if((before&FeatureFixedMembers)&&!(wanted&FeatureFixedMembers)&&!CanDisableFixedMembers(fixedGuard)){
        fail(fixedGuard.valid?ControlMessage::FixedMembersNeedRestoring:ControlMessage::CannotVerifyParties);
        Log("Module deactivation rejected: fixed reserve members must be restored first.");return true;
    }
    const NativeFeatureState native{(wanted&FeatureFixedMembers)!=0,(wanted&FeatureAnywhere)!=0,(wanted&FeatureUnavailable)!=0};
    const bool success=ApplyNativeFeaturesOnGameThread(native);stateKnown.store(NativeFeatureStateKnown());
    if(!success||!stateKnown.load()){fail(ControlMessage::ApplyFailed);return true;}
    applied.store(wanted);
    activityMessage.store(ControlMessage::None);
    if(enabling){
        // 停用不改 requested 或 INI；重新启用回放玩家原偏好。停用前尚未应用
        // 的明确设置请求仍按原规则保存一次，已保存偏好则不额外重写。
        const auto currentRevision=revision.load();
        if(currentRevision!=attemptedRevision.load()){
            attemptedRevision.store(currentRevision);
            if(!SaveFeaturePreferences(wanted))activityMessage.store(ControlMessage::AppliedNotSaved);
        }
        SetRosterRequestsEnabled(true);
        TickRosterOnGameThread(context,actionMask,handled,(wanted&FeatureUnjoined)!=0,(wanted&FeatureAnywhere)!=0);
        moduleActivity.store(1,std::memory_order_release);
        Log("Party module reactivated on a safe game frame; preferences retained.");
    }else{
        // 零掩码仅恢复本实例拥有的代码操作数；不移除常驻 hook、不改角色名单，
        // 已经加入/补足的角色数据及玩家存档保持现状。
        moduleActivity.store(0,std::memory_order_release);
        Log("Party module inactive: native rules restored; resident hooks now pass through.");
    }
    return true;
}

bool OnFieldInput(uintptr_t context, uint32_t actionMask) noexcept {
    const bool handled = originalFieldInput(context, actionMask);
    DWORD expected = 0;
    const DWORD current = GetCurrentThreadId();
    gameThread.compare_exchange_strong(expected, current);
    if (gameThread.load() != current) return handled;
    try {
        const int32_t activity=moduleActivity.load(std::memory_order_acquire);
        if(activity==0)return handled; // 已停用的常驻协调器只转发原生输入。
        const uint64_t pendingRevision = revision.load();
        const bool featureSafe = IsFeatureEditingSafe(context, actionMask, handled);
        const auto fixedGuard = featureSafe ? InspectFixedMemberGuard() : FixedMemberGuardResult{};
        {
            std::lock_guard<std::mutex> guard(messageLock);
            lastFixedGuard = fixedGuard;
            fixedGuardCapturedAt = featureSafe ? GetTickCount64() : 0;
        }
        if(ProcessActivityRequest(activity,featureSafe,fixedGuard,context,actionMask,handled))return handled;
        // 开关只修改已审计代码条件，不是角色加入；编成专属剧情锁由名单服务单独检查，
        // 避免在可自由探索但暂禁编成的存档中把全部设置永远留在待应用状态。
        if (pendingRevision != attemptedRevision.load() && featureSafe) {
            const uint32_t wanted = requested.load() & kAllFeatures;
            // 原生后备列表会隐藏仍带固定标记的后备。只有确证四队均无此状态时，
            // 才允许从开启变为关闭；绝不通过清角色标记或重排队伍来伪造兼容。
            // 旧存档启动时若配置原本已关，仍允许其他功能启动，玩家可显式重新开启
            // 解除固定来找回隐藏角色。关闭失败不保存INI，也不每帧重试。
            const uint32_t before = applied.load();
            const bool fixedClosing = (before & FeatureFixedMembers) && !(wanted & FeatureFixedMembers);
            if (fixedClosing && !CanDisableFixedMembers(fixedGuard)) {
                attemptedRevision.store(pendingRevision);
                uint32_t expectedRequest = wanted;
                // 只撤回本次请求；渲染线程若已提交新值，留给下一修订号处理。
                requested.compare_exchange_strong(expectedRequest, before);
                SetStatus(fixedGuard.valid ?
                    ControlMessage::FixedMembersNeedRestoring : ControlMessage::CannotVerifyParties);
                Log("Fixed-member restriction kept unlocked: reserve compatibility check rejected closing; no party data changed.");
            } else {
                const NativeFeatureState native{(wanted & FeatureFixedMembers) != 0,
                    (wanted & FeatureAnywhere) != 0, (wanted & FeatureUnavailable) != 0};
                // 同一请求失败后不每帧重试或刷日志；用户下一次明确切换才产生新的修订号。
                const bool success = ApplyNativeFeaturesOnGameThread(native);
                attemptedRevision.store(pendingRevision);
                stateKnown.store(NativeFeatureStateKnown());
                if (success) {
                    applied.store(wanted);
                    // 每条请求只记录一次成功，便于区分“协调入口已安装”和“实际设置已生效”。
                    Log("Requested feature switches applied on a safe exploration frame.");
                    if (SaveFeaturePreferences(wanted)) SetStatus(ControlMessage::Applied);
                    else SetStatus(ControlMessage::AppliedNotSaved);
                } else {
                    SetStatus(ControlMessage::ApplyFailed);
                    Log("Feature request failed; no automatic retry and no roster addition in unknown state.");
                }
            }
        }
        // 未入队开关只有前面完整设置事务成功后才生效。异常状态下禁用角色操作，
        // 避免面板显示开启但实际菜单规则不一致时仍写入名单。
        TickRosterOnGameThread(context, actionMask, handled,
            moduleActivity.load()==1 && stateKnown.load() && (applied.load() & FeatureUnjoined) != 0,
            moduleActivity.load()==1 && stateKnown.load() && (applied.load() & FeatureAnywhere) != 0);
    } catch (...) {
        stateKnown.store(false);
        SetStatus(ControlMessage::ServiceError);
        const auto activity=moduleActivity.load();
        if(activity==2||activity==3){
            activityMessage.store(ControlMessage::ServiceError);
            SetRosterRequestsEnabled(true);
            moduleActivity.store(1,std::memory_order_release);
        }
        Log("Control service exception contained.");
    }
    return handled;
}
}

ControlSnapshot ReadControlSnapshot() noexcept {
    ControlSnapshot result;
    result.requestedFeatures = requested.load();
    result.appliedFeatures = applied.load();
    result.appliedStateKnown = stateKnown.load();
    result.waitingForSafeState = revision.load() != attemptedRevision.load();
    {
        std::lock_guard<std::mutex> guard(messageLock);
        result.message = status;
        result.fixedGuard = lastFixedGuard;
        const auto now = GetTickCount64();
        result.fixedGuardFresh = fixedGuardCapturedAt != 0 && now >= fixedGuardCapturedAt &&
            now - fixedGuardCapturedAt <= 500;
    }
    result.roster = ReadRosterSnapshot();
    return result;
}

void RequestFeatureMask(uint32_t features) noexcept {
    if(moduleActivity.load(std::memory_order_acquire)!=1)return;
    requested.store(features & kAllFeatures);
    revision.fetch_add(1);
    SetStatus(ControlMessage::WaitingForExploration);
}

bool RequestModuleActivity(bool enabled) noexcept {
    const int32_t before=moduleActivity.load(std::memory_order_acquire);
    if(before==2||before==3)return false;
    if((before==1)==enabled)return true;
    if(!stateKnown.load()){activityMessage.store(ControlMessage::ApplyFailed);return false;}
    activityMessage.store(ControlMessage::WaitingForExploration);
    // 此操作只修改队列值和发布状态，不读取游戏对象。即使下一帧暂不渲染或
    // 安全协调器暂不被调用，旧确认请求也不会留到未来重新启用时执行。
    SetRosterRequestsEnabled(false);
    moduleActivity.store(enabled?2:3,std::memory_order_release);return true;
}
int32_t ModuleActivityState() noexcept {return moduleActivity.load(std::memory_order_acquire);}
ControlMessage ModuleActivityMessage() noexcept {return activityMessage.load();}

bool InstallControlService(uintptr_t base, uint32_t initialFeatures) noexcept {
    const auto target = reinterpret_cast<void*>(base + 0x2D4D40);
    if (!MatchTickSignature(reinterpret_cast<uintptr_t>(target))) {
        Log("Exploration control tick signature mismatch; coordinator not installed.");
        return false;
    }
    if (!ConfigureFixedMemberGuard(base)) return false;
    requested.store(initialFeatures & kAllFeatures);
    applied.store(0);
    revision.store(1);
    attemptedRevision.store(0);
    moduleActivity.store(1);activityMessage.store(ControlMessage::None);SetRosterRequestsEnabled(true);
    SetStatus(ControlMessage::WaitingForSave);
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&OnFieldInput),
        reinterpret_cast<void**>(&originalFieldInput)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
    Log("Game-thread control coordinator installed.");
    return true;
}
}
