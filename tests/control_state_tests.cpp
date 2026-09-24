// 原生协调器隔离测试：用 MinHook mock 获取回调，不安装真实 hook、不执行游戏代码。
// roster、动态代码修改及 INI 保存也由 mock 接管，检查跨功能的先后顺序和失败边界。
#include <Windows.h>
#include <MinHook.h>
#include "control_state.h"
#include "feature_state.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace sky2party;
namespace {
using Tick = bool(*)(uintptr_t, uint32_t);
Tick capturedDetour = nullptr;
bool safe = false, handled = false, applySuccess = true, nativeKnown = true, saveSuccess = true;
bool lastAllowed = false, lastAnywhere = false;
uint32_t savedMask = 0;
int applyCalls = 0, tickCalls = 0, saveCalls = 0, originalCalls = 0;
int checks = 0, failures = 0;
NativeFeatureState lastFeatures{};
FixedMemberGuardResult guardFixture{true, FixedMemberGuardFailure::None};
int guardCalls = 0;
std::vector<std::string> events;

void Check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAILED: %s\n", description); }
}
bool OriginalFieldInput(uintptr_t, uint32_t) {
    ++originalCalls;
    events.emplace_back("original");
    return handled;
}
void RunTick() { if (capturedDetour) capturedDetour(0x123456, 0x1BF); }
bool Before(const char* left, const char* right) {
    size_t a = events.size(), b = events.size();
    for (size_t i = 0; i < events.size(); ++i) {
        if (events[i] == left && a == events.size()) a = i;
        if (events[i] == right && b == events.size()) b = i;
    }
    return a < b && b < events.size();
}
}

extern "C" {
MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID detour, LPVOID* original) {
    capturedDetour = reinterpret_cast<Tick>(detour);
    *original = reinterpret_cast<LPVOID>(&OriginalFieldInput);
    return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { capturedDetour = nullptr; return MH_OK; }
}

namespace sky2party {
void Log(const char*) noexcept {}
bool SaveFeaturePreferences(uint32_t mask) noexcept {
    events.emplace_back("save"); ++saveCalls; savedMask = mask; return saveSuccess;
}
bool IsFeatureEditingSafe(uintptr_t, uint32_t, bool wasHandled) noexcept {
    events.emplace_back("safe"); return safe && !wasHandled;
}
bool ApplyNativeFeaturesOnGameThread(const NativeFeatureState& requested) noexcept {
    events.emplace_back("apply"); ++applyCalls; lastFeatures = requested; return applySuccess;
}
bool NativeFeatureStateKnown() noexcept { return nativeKnown; }
bool ConfigureFixedMemberGuard(uintptr_t base) noexcept { return base != 0; }
FixedMemberGuardResult InspectFixedMemberGuard() noexcept {
    events.emplace_back("fixed-guard"); ++guardCalls; return guardFixture;
}
void TickRosterOnGameThread(uintptr_t, uint32_t, bool, bool allowed, bool anywhere) noexcept {
    events.emplace_back("roster"); ++tickCalls; lastAllowed = allowed; lastAnywhere = anywhere;
}
RosterSnapshot ReadRosterSnapshot() noexcept { return {}; }
}

int main() {
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2D6000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    constexpr uint8_t signature[]{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,
        0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90,0x00,
        0x00,0x00,0x0F,0x29,0x70,0xC8};
    const auto base = reinterpret_cast<uintptr_t>(image);
    Check(!InstallControlService(base, kDefaultFeatures), "签名不匹配不能创建协调 hook");
    Check(capturedDetour == nullptr, "失败没有遗留可执行回调");
    std::memcpy(image + 0x2D4D40, signature, sizeof(signature));
    Check(InstallControlService(base, kDefaultFeatures) && capturedDetour, "正确签名捕获 mock 回调");

    RunTick();
    Check(applyCalls == 0 && tickCalls == 1 && !lastAllowed, "不安全探索帧不应用开关，加入仍禁用");
    Check(ReadControlSnapshot().waitingForSafeState, "未应用请求保留等待状态");
    safe = true; handled = true;
    RunTick();
    Check(applyCalls == 0 && !lastAllowed, "原生已处理输入不应用设置");
    handled = false; events.clear();
    RunTick();
    Check(applyCalls == 1 && saveCalls == 1 && savedMask == 7, "首个安全帧应用并保存默认三功能");
    Check(lastFeatures.fixedMembers && lastFeatures.anywhere && lastFeatures.unlockUnavailable,
        "功能位映射到对应原生门槛");
    Check(Before("original", "apply") && Before("apply", "roster"), "先原生输入，再设置事务，最后角色队列");
    Check(!lastAllowed && ReadControlSnapshot().appliedFeatures == 7, "默认未入队功能关闭");
    Check(lastAnywhere, "角色服务收到已成功应用的随处编成状态");
    Check(!ReadControlSnapshot().waitingForSafeState, "成功后等待标记清除");

    RequestFeatureMask(15); RunTick();
    Check(lastAllowed && ReadControlSnapshot().appliedFeatures == 15, "开启未入队后队列收到已应用许可");
    RequestFeatureMask(13); RunTick();
    Check(lastAllowed && !lastAnywhere, "未入队与随处编成分开传递，关闭后恢复119限制");
    safe = false; RequestFeatureMask(15); RunTick();
    Check(!lastAnywhere, "仅待应用的随处编成请求不得提前绕过119");
    safe = true; RunTick();
    Check(lastAnywhere, "安全帧成功提交后才给出119入口覆盖许可");
    RequestFeatureMask(7); RunTick();
    Check(!lastAllowed && ReadControlSnapshot().appliedFeatures == 7, "关闭未入队后同帧传入禁用，服务可取消排队");

    applySuccess = false; nativeKnown = false;
    RequestFeatureMask(15); RunTick();
    const int failedAttempts = applyCalls;
    Check(!lastAllowed && !ReadControlSnapshot().appliedStateKnown, "事务状态未知时禁止角色加入");
    Check(!lastAnywhere, "事务状态未知也禁止119入口覆盖");
    Check(ReadControlSnapshot().appliedFeatures == 7, "失败不能宣称请求状态已经应用");
    RunTick(); RunTick();
    Check(applyCalls == failedAttempts, "同一失败请求不会每帧自动重试");
    Check(!lastAllowed, "失败后的后续帧仍禁止加入");

    applySuccess = true; nativeKnown = true;
    RequestFeatureMask(15); RunTick();
    Check(applyCalls == failedAttempts + 1 && lastAllowed, "下一次明确请求可以重新应用");
    Check(ReadControlSnapshot().appliedStateKnown, "成功后重新确认状态已知");

    saveSuccess = false;
    RequestFeatureMask(14); RunTick();
    const auto saveFailed = ReadControlSnapshot();
    Check(saveFailed.appliedFeatures == 14 && lastAllowed && saveFailed.appliedStateKnown,
        "配置文件保存失败不撤销已经生效的游戏内设置");
    Check(std::strstr(saveFailed.message.data(), "配置保存失败") != nullptr, "保存失败有准确提示");
    Check(!lastFeatures.fixedMembers && lastFeatures.anywhere && lastFeatures.unlockUnavailable,
        "独立关闭固定成员不混淆其他开关");

    // 固定队员回到后备会被原版列表隐藏；关闭保护必须位于代码事务/INI保存之前。
    // 本测试只模拟只读检查结果，绝不通过原生交换或写培养数据来恢复角色。
    saveSuccess = true;
    RequestFeatureMask(15); RunTick();
    guardFixture.riskCount = guardFixture.memberCount = 1;
    guardFixture.members[0] = {0, 0, 4, 0x60};
    RunTick();
    Check(ReadControlSnapshot().fixedGuardFresh && ReadControlSnapshot().fixedGuard.riskCount == 1,
        "没有新设置请求时也刷新固定后备提示");
    const int beforeBlockedApply = applyCalls, beforeBlockedSave = saveCalls;
    RequestFeatureMask(14); RunTick();
    const auto blocked = ReadControlSnapshot();
    Check(applyCalls == beforeBlockedApply && saveCalls == beforeBlockedSave,
        "固定后备存在时拒绝关闭，不进入代码修改或配置保存");
    Check(blocked.appliedFeatures == 15 && blocked.requestedFeatures == 15 && !blocked.waitingForSafeState,
        "关闭被拒后界面恢复实际开关，不虚假显示等待关闭");
    Check(std::strstr(blocked.message.data(), "换回主力") != nullptr,
        "关闭失败给出可执行的原生恢复步骤");
    RunTick(); RunTick();
    Check(applyCalls == beforeBlockedApply && saveCalls == beforeBlockedSave,
        "同一被拒关闭请求不会每帧重试或写配置");
    RequestFeatureMask(13); RunTick();
    Check(ReadControlSnapshot().appliedFeatures == 13,
        "固定后备存在仍可独立修改其他功能，不把只读风险当作全面停用");
    RequestFeatureMask(15); RunTick();
    guardFixture.valid = false;
    guardFixture.failure = FixedMemberGuardFailure::ContextChanged;
    RequestFeatureMask(14); RunTick();
    Check(ReadControlSnapshot().appliedFeatures == 15 &&
        std::strstr(ReadControlSnapshot().message.data(), "无法核实") != nullptr,
        "数据未知不等于零风险，拒绝关闭并说明未核实");
    guardFixture = {true, FixedMemberGuardFailure::None};
    events.clear(); RequestFeatureMask(14); RunTick();
    Check(ReadControlSnapshot().appliedFeatures == 14 && savedMask == 14 && Before("fixed-guard", "apply"),
        "换回主力后先核实四队，再允许关闭并持久化设置");
    const int safeGuardCalls = guardCalls;
    safe = false; RunTick();
    Check(guardCalls == safeGuardCalls && !ReadControlSnapshot().fixedGuardFresh,
        "不安全探索帧既不读取队伍，也不沿用旧兼容结论");
    safe = true; RunTick();
    Sleep(550);
    Check(!ReadControlSnapshot().fixedGuardFresh,
        "菜单或换图不再调用协调器时，兼容快照半秒后失效");

    const int previousTickCalls = tickCalls, previousOriginalCalls = originalCalls;
    std::thread otherThread([] { RunTick(); }); otherThread.join();
    Check(originalCalls == previousOriginalCalls + 1 && tickCalls == previousTickCalls,
        "不同线程回调只运行原生函数，不执行游戏数据服务");

    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("%d coordinator checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
