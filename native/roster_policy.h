// 角色加入的纯规则：不读取游戏地址、不调用原生函数，便于覆盖边界测试。
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sky2party {
inline constexpr uint32_t kNoRosterId = 0xFFFFFFFFu;
struct RosterDefinition {
    uint32_t id;
    bool guest;
};
// 名单只含游戏中已有可战斗定义、独立状态槽和模型的十四人，不接受任意 NPC ID。
// 身份与显示文本分离：角色名只能从玩家游戏的对应语言资源按 ID 取得，
// 不在规则表中维护简称或自行翻译的副本，语言切换也不会改变名单顺序或身份。
inline constexpr std::array<RosterDefinition, 14> kRosterDefinitions{{
    {0, false}, {1, false}, {2, false}, {3, false}, {4, false},
    {5, false}, {6, false}, {7, false}, {119, false},
    {100, true}, {101, true}, {106, true}, {107, true}, {112, true}
}};

constexpr size_t RosterIndex(uint32_t id) noexcept {
    for (size_t i = 0; i < kRosterDefinitions.size(); ++i)
        if (kRosterDefinitions[i].id == id) return i;
    return kRosterDefinitions.size();
}

enum class RosterResult : uint8_t {
    None, Queued, AddedReserve, RevealedReserve, AlreadyPresent,
    UnsupportedCharacter, Uninitialized, InvalidData, Full,
    NotReady, UnsafeState, StaleRequest, Busy, NativeRejected, PreparationFailed, PartiallyPrepared,
    PreparedAndAdded, PreparedAndRevealed
};

// “有培养”必须来自精确 ID 记录。查找器的表格回退可能只返回 FFFF 占位槽，
// 因而不能单凭地址非空或等级非零判断可用。此处不要求满血、满 EP 或已装回路。
struct RosterRecordFacts {
    uint32_t requestedId = kNoRosterId;
    uint32_t storedId = kNoRosterId;
    uint32_t level = 0;
    uint32_t maxHp = 0;
    uint32_t maxEp = 0;
    uint32_t maxCp = 0;
    uint32_t weapon = 0;
    bool hasCraft = false;
    bool duplicate = false;
};

constexpr bool HasRosterCoreData(const RosterRecordFacts& facts) noexcept {
    return RosterIndex(facts.requestedId) != kRosterDefinitions.size() &&
        !facts.duplicate && facts.storedId == facts.requestedId &&
        facts.level > 0 && facts.level <= 999 && facts.maxHp > 0 &&
        facts.maxHp <= 100000000 && facts.maxEp > 0 && facts.maxEp <= 1000000 &&
        facts.maxCp > 0 && facts.maxCp <= 1000000;
}
constexpr bool IsInitializedRosterRecord(const RosterRecordFacts& facts) noexcept {
    return HasRosterCoreData(facts) &&
        facts.weapon != 0 && facts.weapon != 0xFFFF && facts.weapon != kNoRosterId &&
        facts.hasCraft;
}

struct RosterAddFacts {
    uint32_t id = kNoRosterId;
    uint32_t count = 0;
    uint32_t memberFlags = 0;
    bool initialized = false;
    bool inParty = false;
    bool validParty = false;
};

// 只有用户逐人请求才会来到这里。已存在的可见成员不被改为后备；隐藏成员必须
// 原本就在后备且无随行类别位，才能仅清隐藏位。这样不暗中挤走任何现有主力。
constexpr RosterResult PlanRosterAdd(const RosterAddFacts& facts) noexcept {
    if (RosterIndex(facts.id) == kRosterDefinitions.size()) return RosterResult::UnsupportedCharacter;
    if (!facts.validParty) return RosterResult::InvalidData;
    if (facts.inParty) {
        if ((facts.memberFlags & 0x800) == 0) return RosterResult::AlreadyPresent;
        if ((facts.memberFlags & 0x40) == 0 || (facts.memberFlags & 0xB) != 0)
            return RosterResult::InvalidData;
        if (!facts.initialized) return RosterResult::Uninitialized;
        return RosterResult::RevealedReserve;
    }
    if (!facts.initialized) return RosterResult::Uninitialized;
    return facts.count < 64 ? RosterResult::AddedReserve : RosterResult::Full;
}

constexpr uint32_t RevealedReserveFlags(uint32_t original) noexcept {
    // 固定 0x20、暂禁 0x80 和所有未知位仍归游戏所有；只解除本次请求的隐藏位。
    return original & ~0x800u;
}

// 请求只绑定短暂的同一场景快照；不跨换图、读档或长时间离开探索后延迟执行。
constexpr bool RosterRequestStillCurrent(uint64_t now, uint64_t queuedAt,
    uint64_t requestedGeneration, uint64_t currentGeneration) noexcept {
    return requestedGeneration != 0 && requestedGeneration == currentGeneration &&
        now >= queuedAt && now - queuedAt <= 2000;
}
}
