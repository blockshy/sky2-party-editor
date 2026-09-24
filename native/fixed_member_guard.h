// 关闭“解除固定队员”前的只读兼容性检查。
// 原生后备列表会隐藏仍带 0x20 的角色；本文件只发现这种状态，不清剧情标记、不改名单。
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace sky2party {
inline constexpr size_t kFixedGuardPartyCount = 4;
inline constexpr size_t kFixedGuardPartyCapacity = 64;
inline constexpr size_t kFixedGuardDetailCapacity = 16;
inline constexpr uintptr_t kFixedGuardManagerRva = 0xC60E08;

enum class FixedMemberGuardFailure : uint8_t {
    None, NotConfigured, GameUnavailable, UnreadableMemory, InvalidCurrentParty,
    InvalidPartyCount, InvalidPartyLimit, InvalidLeader, InvalidMemberId, DuplicateMemberId,
    UnsupportedFixedFlags, ContextChanged, AddressOverflow
};

struct FixedMemberGuardEntry {
    uint32_t id = 0;
    uint32_t partyIndex = 0;
    uint32_t slot = 0;
    uint32_t flags = 0;
};

struct FixedMemberGuardResult {
    // valid=false 表示不能确认结果，绝不等同于“未发现风险”。riskCount 仅在 valid=true 时完整。
    bool valid = false;
    FixedMemberGuardFailure failure = FixedMemberGuardFailure::NotConfigured;
    uint32_t riskCount = 0;
    uint32_t memberCount = 0;
    bool truncated = false;
    std::array<FixedMemberGuardEntry, kFixedGuardDetailCapacity> members{};
    // 出错对象只供提示和诊断，不应以此推断其他对象已经安全。
    uint32_t failureParty = UINT32_MAX;
    uint32_t failureId = UINT32_MAX;
};

constexpr bool CanDisableFixedMembers(const FixedMemberGuardResult& result) noexcept {
    return result.valid && result.riskCount == 0;
}

// base 必须由现有运行时的完整 EXE 身份校验取得；此处不重复读取游戏文件或计算哈希。
bool ConfigureFixedMemberGuard(uintptr_t verifiedBase) noexcept;
FixedMemberGuardResult InspectFixedMemberGuard() noexcept;

namespace fixedguard_detail {
struct Member { uint32_t id = 0; uint32_t flags = 0; };
struct PartyDescriptor {
    uintptr_t members = 0;
    uint32_t count = 0;
    uint32_t opaque = 0; // +0x20C 原样参与稳定性复核，不假定其具有容量含义。
    uint32_t leader = 0;
    uint32_t nativeLimit = 0;
};
static_assert(sizeof(Member) == 8, "Native member layout must be 8 bytes");
static_assert(sizeof(PartyDescriptor) == 24 && offsetof(PartyDescriptor, nativeLimit) == 20,
    "Native party descriptor requires Windows x64 layout");

inline bool Add(uintptr_t address, uintptr_t offset, uintptr_t& result) noexcept {
    if (address > std::numeric_limits<uintptr_t>::max() - offset) return false;
    result = address + offset;
    return true;
}
template<class Reader>
bool Read(Reader& reader, uintptr_t address, void* output, size_t length) noexcept {
    return address != 0 && address <= std::numeric_limits<uintptr_t>::max() - length &&
        reader.Read(address, output, length);
}
}

// Reader 的 Read(address,output,size) 只能读取；生产层以 SEH 防护本进程内存读取，
// 测试层只提供私有合成对象。这种分层使容量、重复 ID、切换中的快照都可脱离游戏验证。
template<class Reader>
FixedMemberGuardResult InspectFixedMemberGuardFromMemory(Reader& reader, uintptr_t verifiedBase) noexcept {
    using namespace fixedguard_detail;
    FixedMemberGuardResult result{};
    const auto fail = [&](FixedMemberGuardFailure why, uint32_t party = UINT32_MAX, uint32_t id = UINT32_MAX) {
        result.valid = false; result.failure = why; result.failureParty = party; result.failureId = id;
        return result;
    };
    if (!verifiedBase) return fail(FixedMemberGuardFailure::NotConfigured);
    uintptr_t managerSlot = 0;
    if (!Add(verifiedBase, kFixedGuardManagerRva, managerSlot)) return fail(FixedMemberGuardFailure::AddressOverflow);
    uintptr_t manager = 0;
    if (!Read(reader, managerSlot, &manager, sizeof(manager))) return fail(FixedMemberGuardFailure::UnreadableMemory);
    if (!manager) return fail(FixedMemberGuardFailure::GameUnavailable);
    uintptr_t partyPointerSlot = 0, currentPartySlot = 0;
    if (!Add(manager, 0x650, partyPointerSlot) || !Add(manager, 0x658, currentPartySlot))
        return fail(FixedMemberGuardFailure::AddressOverflow);
    uintptr_t parties = 0;
    uint32_t current = UINT32_MAX;
    if (!Read(reader, partyPointerSlot, &parties, sizeof(parties)) ||
        !Read(reader, currentPartySlot, &current, sizeof(current)))
        return fail(FixedMemberGuardFailure::UnreadableMemory);
    if (!parties) return fail(FixedMemberGuardFailure::GameUnavailable);
    if (current >= kFixedGuardPartyCount) return fail(FixedMemberGuardFailure::InvalidCurrentParty);

    std::array<PartyDescriptor, kFixedGuardPartyCount> descriptors{};
    std::array<std::array<Member, kFixedGuardPartyCapacity>, kFixedGuardPartyCount> snapshots{};
    std::array<uintptr_t, kFixedGuardPartyCount> descriptorAddresses{};
    for (uint32_t party = 0; party < kFixedGuardPartyCount; ++party) {
        uintptr_t descriptorAddress = 0;
        if (!Add(parties, static_cast<uintptr_t>(party) * 0x218 + 0x200, descriptorAddress))
            return fail(FixedMemberGuardFailure::AddressOverflow, party);
        descriptorAddresses[party] = descriptorAddress;
        auto& descriptor = descriptors[party];
        if (!Read(reader, descriptorAddress, &descriptor, sizeof(descriptor)))
            return fail(FixedMemberGuardFailure::UnreadableMemory, party);
        if (descriptor.count > kFixedGuardPartyCapacity)
            return fail(FixedMemberGuardFailure::InvalidPartyCount, party);
        // 原生 SetMaxCount 的上限为8，战斗主力仍不超过4；0保留为原生边界，不擅自改成4。
        if (descriptor.nativeLimit > 8)
            return fail(FixedMemberGuardFailure::InvalidPartyLimit, party);
        // 原生有效领队必须属于现有前四个槽。空队的领队字段可能是默认值/哨兵，
        // 此时没有角色可丢失，不把该字段当作可解引用的索引。
        if (descriptor.count && (descriptor.leader >= descriptor.count || descriptor.leader >= 4))
            return fail(FixedMemberGuardFailure::InvalidLeader, party);
        if (descriptor.count && !Read(reader, descriptor.members, snapshots[party].data(), descriptor.count * sizeof(Member)))
            return fail(FixedMemberGuardFailure::UnreadableMemory, party);
        const uint32_t activeLimit = std::min<uint32_t>(4, descriptor.nativeLimit);
        for (uint32_t slot = 0; slot < descriptor.count; ++slot) {
            const auto& member = snapshots[party][slot];
            if (member.id >= 0xFFFF)
                return fail(FixedMemberGuardFailure::InvalidMemberId, party, member.id);
            for (uint32_t previous = 0; previous < slot; ++previous)
                if (snapshots[party][previous].id == member.id)
                    return fail(FixedMemberGuardFailure::DuplicateMemberId, party, member.id);
            if (!(member.flags & 0x20)) continue;
            // 低位0x01/0x02/0x08涉及原生特殊类别。没有可靠恢复证据时返回未知，
            // 不能把这些固定条目算作普通可换回的四人主力，也不能自动清除其分类位。
            if (member.flags & 0x0B)
                return fail(FixedMemberGuardFailure::UnsupportedFixedFlags, party, member.id);
            if ((member.flags & 0x40) || slot >= activeLimit) {
                ++result.riskCount;
                if (result.memberCount < result.members.size())
                    result.members[result.memberCount++] = {member.id, party, slot, member.flags};
                else result.truncated = true;
            }
        }
    }

    // 调用方位于协调游戏线程；仍二读全4队的描述符、条目及场景指针，避免读档/换队
    // 或其他插件导致前后混合的快照被误判安全。这里没有写入或触发原生排序等副作用。
    for (uint32_t party = 0; party < kFixedGuardPartyCount; ++party) {
        PartyDescriptor descriptor{};
        if (!Read(reader, descriptorAddresses[party], &descriptor, sizeof(descriptor)))
            return fail(FixedMemberGuardFailure::UnreadableMemory, party);
        if (std::memcmp(&descriptor, &descriptors[party], sizeof(descriptor)))
            return fail(FixedMemberGuardFailure::ContextChanged, party);
        std::array<Member, kFixedGuardPartyCapacity> members{};
        if (descriptor.count && !Read(reader, descriptor.members, members.data(), descriptor.count * sizeof(Member)))
            return fail(FixedMemberGuardFailure::UnreadableMemory, party);
        if (std::memcmp(members.data(), snapshots[party].data(), descriptor.count * sizeof(Member)))
            return fail(FixedMemberGuardFailure::ContextChanged, party);
    }
    uintptr_t finalManager = 0, finalParties = 0;
    uint32_t finalCurrent = UINT32_MAX;
    if (!Read(reader, managerSlot, &finalManager, sizeof(finalManager)) ||
        !Read(reader, partyPointerSlot, &finalParties, sizeof(finalParties)) ||
        !Read(reader, currentPartySlot, &finalCurrent, sizeof(finalCurrent)))
        return fail(FixedMemberGuardFailure::UnreadableMemory);
    if (manager != finalManager || parties != finalParties || current != finalCurrent)
        return fail(FixedMemberGuardFailure::ContextChanged);
    result.valid = true;
    result.failure = FixedMemberGuardFailure::None;
    return result;
}
}
