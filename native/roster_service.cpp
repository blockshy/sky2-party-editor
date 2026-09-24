// 所有游戏数据读取、名单变更均发生在已审核的探索输入回调之后。
// 绘制线程与工作线程只能访问 g_snapshot 的副本，不能持有原生对象地址。
#include "roster_service.h"
#include "roster_prepare.h"
#include "runtime.h"
#include <Windows.h>
#include <array>
#include <cstring>

namespace sky2party {
namespace {
uintptr_t g_base = 0;
SRWLOCK g_lock = SRWLOCK_INIT;
RosterSnapshot g_snapshot{};
uint64_t g_generation = 0;
uint64_t g_lastTick = 0;
bool g_wasSafe = false;
bool g_processing = false;
struct Member { uint32_t id, flags; };
static_assert(sizeof(Member) == 8);
struct Identity {
    uintptr_t field = 0, savedata = 0, statusManager = 0, place = 0, party = 0, members = 0;
    uint32_t partyIndex = 0, count = 0, leader = 0, nativeLimit = 0;
    std::array<char, 32> scene{};
    std::array<Member, 64> roster{};
};
Identity g_identity{};
struct Request {
    uint32_t id = kNoRosterId;
    uint64_t queuedAt = 0, generation = 0;
    Identity identity{};
};
Request g_request{};
struct Capture {
    Identity identity{};
    std::array<RosterRecordFacts, 14> facts{};
    std::array<uintptr_t, 14> statusAddresses{};
    std::array<int, 14> memberIndices{};
    RosterSnapshot snapshot{};
};

// 引擎对象必须在调用线程有效；SEH 只将无效/读档途中地址变成不可操作状态。
// 它不是访问任意地址的授权，也不通过 VirtualProtect 修改对象页。
template<class T> T At(uintptr_t address) noexcept {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
bool SameIdentity(const Identity& a, const Identity& b) noexcept {
    return a.field == b.field && a.savedata == b.savedata && a.statusManager == b.statusManager &&
        a.place == b.place && a.party == b.party && a.members == b.members &&
        a.partyIndex == b.partyIndex && a.count == b.count && a.leader == b.leader &&
        a.nativeLimit == b.nativeLimit && a.scene == b.scene &&
        std::memcmp(a.roster.data(), b.roster.data(), a.count * sizeof(Member)) == 0;
}

bool FeatureSafeRaw(uintptr_t context, uint32_t mask, bool handled) noexcept {
    if (!g_base || context < 0x10000 || handled || (mask & 0x10) == 0) return false;
    const auto story = At<uintptr_t>(g_base + 0xC60E58);
    const auto field = At<uintptr_t>(g_base + 0xC60E08);
    const auto runtime = At<uintptr_t>(g_base + 0xC5D768);
    const auto environment = At<uintptr_t>(g_base + 0xC60E68);
    if (!story || !field || !runtime || !environment) return false;
    // 复现原生 2D4C90 / 2D4D40 / OpenCamp 中的拒绝条件；额外排除特殊环境。
    // flag 1 为输入限制、8 为菜单限制。30/119是编成相关门槛，不能阻止开关初次
    // 应用，否则正常可走动但禁止编成的地图会一直保持“等待应用”。
    if ((At<uint8_t>(story + 0x100) & 2) || (At<uint8_t>(story + 0x101) & 1)) return false;
    if (At<uint8_t>(runtime + 0x2D30) || At<uint8_t>(environment + 0x18E) ||
        !At<uint8_t>(context + 0x340) || !(At<uint32_t>(context + 0x320) & 2) ||
        At<uintptr_t>(context + 0xB8) || At<uint8_t>(context + 0x35) ||
        !At<uintptr_t>(context + 0x60)) return false;
    if (At<uintptr_t>(field + 0x718) || At<uint32_t>(field + 0x1BC8) ||
        !At<uintptr_t>(field + 0x648)) return false;
    const auto fieldState = At<uintptr_t>(field + 0x6A8);
    return fieldState && !(At<uint32_t>(fieldState + 0x290) & 0x400);
}

RosterBlockReason FormationBlockRaw(bool allowAnywhere) noexcept {
    const auto story = At<uintptr_t>(g_base + 0xC60E58);
    if (!story) return RosterBlockReason::NotExploring;
    if (At<uint8_t>(story + 0x103) & 0x40) return RosterBlockReason::FormationStoryLock;
    // 随处编成事务已经同步放宽原生入口和HUD的119查询，服务必须采用相同许可。
    // 这里只读真实剧情字节；关闭或状态未知时调用方传false，立即恢复原生门槛。
    if (!allowAnywhere && (At<uint8_t>(story + 0x10E) & 0x80)) return RosterBlockReason::NativeEntryLock;
    return RosterBlockReason::None;
}
RosterBlockReason FormationBlock(bool allowAnywhere) noexcept {
    __try { return FormationBlockRaw(allowAnywhere); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return RosterBlockReason::NotExploring; }
}

bool CaptureRaw(Capture& capture) noexcept {
    auto& identity = capture.identity;
    identity.field = At<uintptr_t>(g_base + 0xC60E08);
    identity.savedata = At<uintptr_t>(g_base + 0xC60E58);
    identity.statusManager = At<uintptr_t>(g_base + 0xC60E50);
    if (!identity.field || !identity.savedata || !identity.statusManager) return false;
    identity.partyIndex = At<uint32_t>(identity.field + 0x658);
    const auto parties = At<uintptr_t>(identity.field + 0x650);
    identity.place = At<uintptr_t>(identity.field + 0x648);
    if (!parties || !identity.place || identity.partyIndex >= 4) return false;
    identity.party = parties + identity.partyIndex * 0x218;
    identity.members = At<uintptr_t>(identity.party + 0x200);
    identity.count = At<uint32_t>(identity.party + 0x208);
    identity.leader = At<uint32_t>(identity.party + 0x210);
    identity.nativeLimit = At<uint32_t>(identity.party + 0x214);
    if (!identity.members || !identity.count || identity.count > 64 ||
        identity.leader >= identity.count || !identity.nativeLimit || identity.nativeLimit > 8) return false;
    std::memcpy(identity.scene.data(), reinterpret_cast<const void*>(identity.field + 0x170), identity.scene.size());
    if (identity.scene[0] == '\0' || !std::memchr(identity.scene.data(), 0, identity.scene.size())) return false;
    std::memcpy(identity.roster.data(), reinterpret_cast<const void*>(identity.members), identity.count * sizeof(Member));
    for (uint32_t i = 0; i < identity.count; ++i) {
        if (identity.roster[i].id == 0xFFFF) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (identity.roster[j].id == identity.roster[i].id) return false;
    }
    capture.memberIndices.fill(-1);
    for (size_t i = 0; i < kRosterDefinitions.size(); ++i) {
        const auto& definition = kRosterDefinitions[i];
        auto& entry = capture.snapshot.members[i];
        entry.id = definition.id;
        entry.name = definition.name;
        entry.guest = definition.guest;
        capture.facts[i].requestedId = definition.id;
        for (uint32_t j = 0; j < identity.count; ++j) {
            if (identity.roster[j].id != definition.id) continue;
            capture.memberIndices[i] = static_cast<int>(j);
            entry.inParty = true;
            entry.hidden = (identity.roster[j].flags & 0x800) != 0;
            entry.unavailable = (identity.roster[j].flags & 0x80) != 0;
        }
    }
    // 原生 43D3A0 的第一层是固定 100 槽精确 ID 搜索；绝不采用其表格占位回退。
    for (size_t slot = 0; slot < 100; ++slot) {
        const auto record = identity.statusManager + 0x1142F8 + slot * 0x2A0;
        const auto id = At<uint32_t>(record);
        const auto index = RosterIndex(id);
        if (index == kRosterDefinitions.size()) continue;
        auto& facts = capture.facts[index];
        if (capture.statusAddresses[index]) { facts.duplicate = true; continue; }
        capture.statusAddresses[index] = record;
        facts.storedId = id;
        facts.level = At<uint32_t>(record + 4);
        facts.maxHp = At<uint32_t>(record + 0x10);
        facts.maxEp = At<uint32_t>(record + 0x18);
        facts.maxCp = At<uint32_t>(record + 0x20);
        facts.weapon = At<uint32_t>(record + 0x264);
        for (size_t byte = 0; byte < 32; ++byte)
            facts.hasCraft = facts.hasCraft || At<uint8_t>(record + 0x16C + byte) != 0;
    }
    for (size_t i = 0; i < kRosterDefinitions.size(); ++i) {
        auto& entry = capture.snapshot.members[i];
        entry.level = capture.facts[i].level;
        entry.initialized = IsInitializedRosterRecord(capture.facts[i]);
        const auto flags = entry.inParty ? identity.roster[capture.memberIndices[i]].flags : 0;
        const bool canPrepare = !entry.initialized && CanPrepareRosterRecord(capture.facts[i]);
        const auto plan = PlanRosterAdd({entry.id, identity.count, flags,
            entry.initialized || canPrepare, entry.inParty, true});
        entry.canAdd = plan == RosterResult::AddedReserve || plan == RosterResult::RevealedReserve;
        entry.needsPreparation = entry.canAdd && canPrepare;
    }
    capture.snapshot.partyIndex = identity.partyIndex;
    capture.snapshot.ready = true;
    return true;
}
bool TryCapture(Capture& capture) noexcept {
    __try { return CaptureRaw(capture); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

RosterResult ApplyRequestRaw(const Request& request, const Capture& capture, bool& preparationChanged) noexcept {
    const auto index = RosterIndex(request.id);
    if (index == kRosterDefinitions.size()) return RosterResult::UnsupportedCharacter;
    if (!SameIdentity(request.identity, capture.identity)) return RosterResult::StaleRequest;
    const auto& before = capture.identity;
    const auto slot = capture.memberIndices[index];
    const auto flags = slot >= 0 ? before.roster[slot].flags : 0;
    const bool initialized = IsInitializedRosterRecord(capture.facts[index]);
    const auto plan = PlanRosterAdd({request.id, before.count, flags,
        initialized || CanPrepareRosterRecord(capture.facts[index]), slot >= 0, true});
    if (plan != RosterResult::AddedReserve && plan != RosterResult::RevealedReserve) return plan;

    // 用户确认的是单人“补足并加入”。只调用已审计的空槽补足流程；已有可见角色在
    // 上方已经返回，不能借反复点击给卸下武器的现役角色重复生成物品。
    bool prepared = false;
    if (!initialized) {
        const auto preparation = PrepareRosterRecordOnGameThread(request.id, capture.statusAddresses[index]);
        if (preparation == RosterPreparationResult::PartialPrepared) return RosterResult::PartiallyPrepared;
        if (preparation != RosterPreparationResult::Prepared && preparation != RosterPreparationResult::NotNeeded)
            return RosterResult::PreparationFailed;
        prepared = preparation == RosterPreparationResult::Prepared;
        preparationChanged = prepared;
        Capture checked{};
        if (!CaptureRaw(checked) || !SameIdentity(before, checked.identity) ||
            checked.statusAddresses[index] != capture.statusAddresses[index] ||
            !IsInitializedRosterRecord(checked.facts[index]))
            return prepared ? RosterResult::PartiallyPrepared : RosterResult::PreparationFailed;
    }

    // 保存“原生加入前”的完整状态，核对 Join 不再触碰培养。空槽补足如已执行，
    // 这里记录其结果，不能把补足前的副本强写回去伪装成跨原生调用的回滚。
    std::array<uint8_t, 0x2A0> oldRecord{};
    std::memcpy(oldRecord.data(), reinterpret_cast<const void*>(capture.statusAddresses[index]), oldRecord.size());
    if (plan == RosterResult::RevealedReserve) {
        // 单人明确请求只解除隐藏，保留后备/固定/暂禁等位。不在这里给主力强行换位。
        const auto address = before.members + static_cast<uintptr_t>(slot) * sizeof(Member) + 4;
        const auto replacement = RevealedReserveFlags(flags);
        if (static_cast<uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(address),
            static_cast<LONG>(replacement), static_cast<LONG>(flags))) != flags)
            return prepared ? RosterResult::PartiallyPrepared : RosterResult::StaleRequest;
    } else {
        // 原生 party_join 的 ABI：RCX=当前 Party，EDX=角色 ID，R8D=成员 flags。
        // AL 在重复/容量分支不是可靠成功码，故忽略返回值，完整核对名单后置条件。
        using Join = void(__fastcall*)(uintptr_t, uint32_t, uint32_t);
        reinterpret_cast<Join>(g_base + 0x2C8ED0)(before.party, request.id, 0x40);
    }
    Capture after{};
    if (!CaptureRaw(after) || after.identity.party != before.party ||
        after.identity.members != before.members || after.identity.leader != before.leader ||
        after.identity.nativeLimit != before.nativeLimit ||
        std::memcmp(oldRecord.data(), reinterpret_cast<const void*>(capture.statusAddresses[index]), oldRecord.size()) != 0)
        return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
    if (plan == RosterResult::RevealedReserve) {
        if (after.identity.count != before.count) return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
        for (uint32_t i = 0; i < before.count; ++i) {
            const auto expectedFlags = static_cast<int>(i) == slot ? RevealedReserveFlags(flags) : before.roster[i].flags;
            if (after.identity.roster[i].id != before.roster[i].id || after.identity.roster[i].flags != expectedFlags)
                return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
        }
    } else {
        if (after.identity.count != before.count + 1) return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
        uint32_t oldIndex = 0, found = 0;
        for (uint32_t i = 0; i < after.identity.count; ++i) {
            const auto& member = after.identity.roster[i];
            if (member.id == request.id) {
                if (member.flags != 0x40) return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
                ++found;
            } else {
                if (oldIndex >= before.count || std::memcmp(&member, &before.roster[oldIndex++], sizeof(Member)) != 0)
                    return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
            }
        }
        if (found != 1 || oldIndex != before.count) return prepared ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
    }
    // 只加入后备，场景上的四名主力未变；不运行 ApplyPartyCharas、不触发剧情脚本。
    if (prepared) return plan == RosterResult::AddedReserve ? RosterResult::PreparedAndAdded : RosterResult::PreparedAndRevealed;
    return plan;
}
RosterResult TryApply(const Request& request, const Capture& capture) noexcept {
    bool preparationChanged = false;
    __try { return ApplyRequestRaw(request, capture, preparationChanged); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // 即使后续 Join 或复核读取异常，也不能隐去此前已经完成的数据补足。
        return preparationChanged ? RosterResult::PartiallyPrepared : RosterResult::NativeRejected;
    }
}
}

bool ConfigureRosterService(uintptr_t base) noexcept {
    constexpr uint8_t expected[]{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x57,0x48,0x83,0xEC,0x20,0x41,0x8B,0xF0,0x8B,0xFA,0x48,0x8B,0xD9,
        0x81,0xFA,0xFF,0xFF,0x00,0x00,0x75,0x30};
    __try {
        if (!base || std::memcmp(reinterpret_cast<const void*>(base + 0x2C8ED0), expected, sizeof(expected)) != 0)
            return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!ConfigureRosterPreparation(base)) return false;
    g_base = base;
    return true;
}

bool IsFeatureEditingSafe(uintptr_t context, uint32_t mask, bool handled) noexcept {
    __try { return FeatureSafeRaw(context, mask, handled); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool IsRosterEditingSafe(uintptr_t context, uint32_t mask, bool handled, bool allowAnywhere) noexcept {
    return IsFeatureEditingSafe(context, mask, handled) && FormationBlock(allowAnywhere) == RosterBlockReason::None;
}

RosterSnapshot ReadRosterSnapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    auto result = g_snapshot;
    ReleaseSRWLockShared(&g_lock);
    // 菜单/过场期间可能没有该输入回调。显示可以保留，旧的可操作许可必须立即失效。
    const auto now = GetTickCount64();
    if (!result.capturedAtMs || now < result.capturedAtMs || now - result.capturedAtMs > 500) {
        result.canEdit = false;
        result.blockReason = RosterBlockReason::NotExploring;
        for (auto& member : result.members) member.canAdd = false;
    }
    return result;
}

bool QueueAddMember(uint32_t id) noexcept {
    const auto now = GetTickCount64();
    const auto index = RosterIndex(id);
    AcquireSRWLockExclusive(&g_lock);
    const bool accept = index != kRosterDefinitions.size() && g_snapshot.ready && g_snapshot.canEdit &&
        g_snapshot.allowUnjoined && g_snapshot.members[index].canAdd && !g_processing &&
        g_request.id == kNoRosterId && now >= g_snapshot.capturedAtMs && now - g_snapshot.capturedAtMs <= 500;
    if (accept) {
        g_request = {id, now, g_snapshot.generation, g_identity};
        g_snapshot.pendingId = id;
        g_snapshot.lastId = id;
        g_snapshot.lastResult = RosterResult::Queued;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return accept;
}

void TickRosterOnGameThread(uintptr_t context, uint32_t mask, bool handled, bool allow, bool allowAnywhere) noexcept {
    const auto now = GetTickCount64();
    const bool featureSafe = IsFeatureEditingSafe(context, mask, handled);
    const auto blockReason = featureSafe ? FormationBlock(allowAnywhere) : RosterBlockReason::NotExploring;
    const bool safe = featureSafe && blockReason == RosterBlockReason::None;
    Capture capture{};
    // 编成剧情锁并不禁止只读查看已有角色。维持真实数据快照，但明确禁用加入按钮。
    const bool captured = featureSafe && TryCapture(capture);
    Request request{};
    RosterResult result = RosterResult::None;
    AcquireSRWLockExclusive(&g_lock);
    // 不安全帧、停更超过半秒、地图对象/队伍内容变化都会推进世代并作废旧请求。
    const bool changed = !safe || !captured || !g_wasSafe || !g_lastTick || now - g_lastTick > 500 ||
        !SameIdentity(g_identity, capture.identity);
    if (changed) ++g_generation;
    g_wasSafe = safe && captured;
    g_lastTick = now;
    if (g_request.id != kNoRosterId) {
        request = g_request;
        g_request = {};
        g_processing = true;
        if (!safe || !captured || !allow) result = RosterResult::UnsafeState;
        else if (!RosterRequestStillCurrent(now, request.queuedAt, request.generation, g_generation))
            result = RosterResult::StaleRequest;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (request.id != kNoRosterId && result == RosterResult::None) result = TryApply(request, capture);
    if (request.id != kNoRosterId) {
        // 添加后的新名单必须重新采样；不能把操作前的“可加入”按钮继续呈现给 UI。
        capture = {};
        if (featureSafe) TryCapture(capture);
        Log(result == RosterResult::AddedReserve ? "Explicit roster request added a trained character to reserve." :
            result == RosterResult::RevealedReserve ? "Explicit roster request revealed a trained reserve character." :
            result == RosterResult::PreparedAndAdded || result == RosterResult::PreparedAndRevealed ?
                "Explicit roster request prepared missing role fields and made the character available in reserve." :
            "Roster request was rejected; no automatic retry will occur.");
    }
    AcquireSRWLockExclusive(&g_lock);
    const auto lastId = request.id != kNoRosterId ? request.id : g_snapshot.lastId;
    const auto lastResult = request.id != kNoRosterId ? result : g_snapshot.lastResult;
    g_snapshot = capture.snapshot;
    g_snapshot.canEdit = safe && capture.snapshot.ready;
    g_snapshot.allowUnjoined = allow;
    g_snapshot.blockReason = blockReason;
    g_snapshot.capturedAtMs = now;
    g_snapshot.generation = g_generation;
    g_snapshot.lastId = lastId;
    g_snapshot.lastResult = lastResult;
    // 若界面在线程采样期间提交了新请求，保留其排队提示；请求体仍由下一帧处理。
    g_snapshot.pendingId = g_request.id;
    for (auto& entry : g_snapshot.members) entry.canAdd = entry.canAdd && g_snapshot.canEdit && allow;
    if (capture.snapshot.ready) g_identity = capture.identity;
    g_processing = false;
    ReleaseSRWLockExclusive(&g_lock);
}

const char* RosterResultText(RosterResult result) noexcept {
    switch (result) {
    case RosterResult::None: return "";
    case RosterResult::Queued: return "等待当前探索帧处理";
    case RosterResult::AddedReserve: return "已加入后备，原有培养保持不变";
    case RosterResult::RevealedReserve: return "已显示在后备，原有培养保持不变";
    case RosterResult::AlreadyPresent: return "角色已经在当前名单中";
    case RosterResult::UnsupportedCharacter: return "不支持此角色";
    case RosterResult::Uninitialized: return "角色数据尚未完整初始化，暂不可加入";
    case RosterResult::InvalidData: return "当前名单或角色数据不符合已验证条件";
    case RosterResult::Full: return "当前队伍名单已满";
    case RosterResult::NotReady: return "等待读取当前角色数据";
    case RosterResult::UnsafeState: return "请回到普通探索画面后重新操作";
    case RosterResult::StaleRequest: return "场景或队伍已变化，请重新确认";
    case RosterResult::Busy: return "上一条加入请求仍在处理";
    case RosterResult::NativeRejected: return "未确认加入成功，请先核对游戏队伍";
    case RosterResult::PreparationFailed: return "缺失数据未能补足，未加入队伍";
    case RosterResult::PartiallyPrepared: return "数据已部分补足；未确认加入成功，请核对角色和队伍";
    case RosterResult::PreparedAndAdded: return "已补足缺失数据并加入后备";
    case RosterResult::PreparedAndRevealed: return "已补足缺失数据并显示在后备";
    }
    return "";
}

const char* RosterBlockReasonText(RosterBlockReason reason) noexcept {
    switch (reason) {
    case RosterBlockReason::None: return "";
    case RosterBlockReason::NotExploring: return "请回到可自由行动的探索画面并关闭游戏菜单";
    case RosterBlockReason::FormationStoryLock: return "当前剧情禁止调整队伍；仍可查看角色和应用 Mod 设置";
    case RosterBlockReason::NativeEntryLock: return "当前入口受限；开启随处编成后可在自由探索时加入角色";
    }
    return "";
}
}
