// 固定队员关闭保护的隔离测试：所有地址均为测试 Reader 的逻辑地址，不接触游戏内存。
#include "fixed_member_guard.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <vector>

using namespace sky2party;
using sky2party::fixedguard_detail::Member;
using sky2party::fixedguard_detail::PartyDescriptor;
static unsigned scenarios = 0;
static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Fixture {
    static constexpr uintptr_t base = 0x100000000ull;
    static constexpr uintptr_t manager = 0x200000000ull;
    static constexpr uintptr_t parties = 0x300000000ull;
    static constexpr uintptr_t memberBase = 0x400000000ull;
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    std::map<uintptr_t, unsigned> reads;
    std::function<void(Fixture&, uintptr_t, unsigned)> beforeRead;
    Fixture() {
        regions[base + kFixedGuardManagerRva].resize(sizeof(uintptr_t));
        regions[manager].resize(0x700);
        regions[parties].resize(0x218 * 4);
        Put(base + kFixedGuardManagerRva, manager);
        Put(manager + 0x650, parties);
        Put(manager + 0x658, uint32_t{0});
        for (uint32_t party = 0; party < 4; ++party) {
            regions[MemberAddress(party)].resize(64 * sizeof(Member));
            SetParty(party, 0, 8);
        }
    }
    static uintptr_t DescriptorAddress(uint32_t party) { return parties + 0x218 * party + 0x200; }
    static uintptr_t MemberAddress(uint32_t party) { return memberBase + 0x1000 * party; }
    template<class Value> void Put(uintptr_t address, const Value& value) {
        for (auto& region : regions) {
            if (address >= region.first && address - region.first <= region.second.size() &&
                sizeof(value) <= region.second.size() - (address - region.first)) {
                std::memcpy(region.second.data() + address - region.first, &value, sizeof(value)); return;
            }
        }
        Require(false, "fixture write must stay within synthetic regions");
    }
    void SetParty(uint32_t party, uint32_t count, uint32_t limit) {
        PartyDescriptor descriptor{MemberAddress(party), count, 0, 0, limit};
        Put(DescriptorAddress(party), descriptor);
        for (uint32_t slot = 0; slot < std::min<uint32_t>(count, 64); ++slot)
            SetMember(party, slot, slot, 0);
    }
    void SetMember(uint32_t party, uint32_t slot, uint32_t id, uint32_t flags) {
        Put(MemberAddress(party) + sizeof(Member) * slot, Member{id, flags});
    }
    bool Read(uintptr_t address, void* output, size_t length) {
        const unsigned count = ++reads[address];
        if (beforeRead) beforeRead(*this, address, count);
        for (const auto& region : regions) {
            if (address >= region.first && address - region.first <= region.second.size() &&
                length <= region.second.size() - (address - region.first)) {
                std::memcpy(output, region.second.data() + address - region.first, length); return true;
            }
        }
        return false;
    }
    FixedMemberGuardResult Inspect() { return InspectFixedMemberGuardFromMemory(*this, base); }
};

int main() {
    {
        Fixture f; const auto before = f.regions; const auto result = f.Inspect();
        Require(CanDisableFixedMembers(result) && result.memberCount == 0, "four empty parties are safe");
        Require(before == f.regions, "inspection is read-only"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 4, 8);
        for (uint32_t slot = 0; slot < 4; ++slot) f.SetMember(0, slot, slot, 0x20);
        Require(CanDisableFixedMembers(f.Inspect()), "four fixed active members remain safe"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.SetMember(0, 0, 119, 0x60);
        const auto result = f.Inspect();
        Require(result.valid && !CanDisableFixedMembers(result) && result.riskCount == 1 &&
            result.memberCount == 1 && result.members[0].id == 119 && result.members[0].flags == 0x60,
            "fixed reserve is risky even in first slot"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 5, 8); f.SetMember(0, 4, 4, 0x20);
        const auto result = f.Inspect();
        Require(result.valid && result.riskCount == 1 && result.members[0].slot == 4,
            "index outside four active slots is risky without reserve bit"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 3, 2); f.SetMember(0, 2, 2, 0x20);
        Require(f.Inspect().riskCount == 1, "native limit below four is respected"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 0); f.SetMember(0, 0, 0, 0x20);
        Require(f.Inspect().valid && f.Inspect().riskCount == 1, "zero native limit cannot create active slot"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(3, 6, 8); f.SetMember(3, 5, 101, 0x60);
        const auto result = f.Inspect();
        Require(result.valid && result.riskCount == 1 && result.members[0].partyIndex == 3,
            "all parties are scanned regardless of current party"); ++scenarios;
    }
    {
        Fixture f;
        for (uint32_t party = 0; party < 4; ++party) {
            f.SetParty(party, 64, 8);
            for (uint32_t slot = 0; slot < 64; ++slot) f.SetMember(party, slot, slot, 0x60);
        }
        const auto result = f.Inspect();
        Require(result.valid && result.riskCount == 256 && result.memberCount == 16 && result.truncated,
            "64-member capacities and result truncation never lose total risk count"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 65, 8);
        Require(f.Inspect().failure == FixedMemberGuardFailure::InvalidPartyCount, "over-capacity rejected"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 9);
        Require(f.Inspect().failure == FixedMemberGuardFailure::InvalidPartyLimit, "unknown native limit rejected"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 2, 8); f.Put(Fixture::DescriptorAddress(0) + 16, uint32_t{2});
        Require(f.Inspect().failure == FixedMemberGuardFailure::InvalidLeader, "leader outside member count rejected"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 8, 8); f.Put(Fixture::DescriptorAddress(0) + 16, uint32_t{4});
        Require(f.Inspect().failure == FixedMemberGuardFailure::InvalidLeader, "leader outside active four slots rejected"); ++scenarios;
    }
    {
        Fixture f; f.Put(Fixture::DescriptorAddress(0) + 16, UINT32_MAX);
        Require(CanDisableFixedMembers(f.Inspect()), "empty party sentinel leader is not dereferenced"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 2, 8); f.SetMember(0, 1, 0, 0x20);
        const auto result = f.Inspect();
        Require(!result.valid && result.failure == FixedMemberGuardFailure::DuplicateMemberId && result.failureId == 0,
            "duplicate ID in one party is unknown"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.SetParty(1, 1, 8);
        Require(CanDisableFixedMembers(f.Inspect()), "same ID in separate parties is not conflated"); ++scenarios;
    }
    for (const uint32_t flags : {0x21u, 0x22u, 0x28u}) {
        Fixture f; f.SetParty(0, 1, 8); f.SetMember(0, 0, 0, flags);
        const auto result = f.Inspect();
        Require(!result.valid && result.failure == FixedMemberGuardFailure::UnsupportedFixedFlags,
            "special fixed category cannot be assumed restorable"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.SetMember(0, 0, 0, 0x0B);
        Require(CanDisableFixedMembers(f.Inspect()), "nonfixed special category is not rewritten or blocked"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.SetMember(0, 0, 0xFFFF, 0);
        Require(f.Inspect().failure == FixedMemberGuardFailure::InvalidMemberId, "invalid sentinel ID rejected"); ++scenarios;
    }
    {
        Fixture f; f.Put(Fixture::manager + 0x658, uint32_t{4});
        Require(f.Inspect().failure == FixedMemberGuardFailure::InvalidCurrentParty, "invalid current party rejected"); ++scenarios;
    }
    {
        Fixture f; f.Put(Fixture::base + kFixedGuardManagerRva, uintptr_t{0});
        Require(f.Inspect().failure == FixedMemberGuardFailure::GameUnavailable, "unloaded game is unknown"); ++scenarios;
    }
    {
        Fixture f; f.Put(Fixture::manager + 0x650, uintptr_t{0});
        Require(f.Inspect().failure == FixedMemberGuardFailure::GameUnavailable, "missing parties are unknown"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.Put(Fixture::DescriptorAddress(0), uintptr_t{0});
        Require(f.Inspect().failure == FixedMemberGuardFailure::UnreadableMemory, "nonempty null member array rejected"); ++scenarios;
    }
    {
        Fixture f; f.regions.erase(Fixture::manager);
        Require(f.Inspect().failure == FixedMemberGuardFailure::UnreadableMemory, "unreadable context rejected"); ++scenarios;
    }
    {
        Fixture f;
        Require(InspectFixedMemberGuardFromMemory(f, 0).failure == FixedMemberGuardFailure::NotConfigured,
            "missing verified host cannot report safe"); ++scenarios;
    }
    {
        Fixture f;
        Require(InspectFixedMemberGuardFromMemory(f, UINTPTR_MAX).failure == FixedMemberGuardFailure::AddressOverflow,
            "host address arithmetic cannot wrap"); ++scenarios;
    }
    for (const uintptr_t target : {Fixture::manager + 0x658, Fixture::manager + 0x650, Fixture::base + kFixedGuardManagerRva}) {
        Fixture f;
        f.beforeRead = [target](Fixture& self, uintptr_t address, unsigned count) {
            if (address == target && count == 2) {
                if (target == Fixture::manager + 0x658) self.Put(address, uint32_t{1});
                else self.Put(address, uintptr_t{0});
            }
        };
        Require(f.Inspect().failure == FixedMemberGuardFailure::ContextChanged,
            "manager/party-array/current-party change invalidates mixed snapshot"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8);
        f.beforeRead = [](Fixture& self, uintptr_t address, unsigned count) {
            if (address == Fixture::DescriptorAddress(0) && count == 2) self.SetParty(0, 2, 8);
        };
        Require(f.Inspect().failure == FixedMemberGuardFailure::ContextChanged, "changed descriptor invalidates snapshot"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.SetMember(0, 0, 0, 0x20);
        f.beforeRead = [](Fixture& self, uintptr_t address, unsigned count) {
            if (address == Fixture::MemberAddress(0) && count == 2) self.SetMember(0, 0, 0, 0x60);
        };
        Require(f.Inspect().failure == FixedMemberGuardFailure::ContextChanged, "changed member flags invalidate snapshot"); ++scenarios;
    }
    {
        Fixture f; f.SetParty(0, 1, 8); f.SetMember(0, 0, 0, 0x20);
        const auto first = f.Inspect(); f.SetMember(0, 0, 0, 0x60); const auto second = f.Inspect();
        Require(CanDisableFixedMembers(first) && !CanDisableFixedMembers(second) && second.riskCount == 1,
            "fresh inspection follows new save/party data without stale baseline"); ++scenarios;
    }
    std::printf("PASS: %u fixed-member guard scenarios; read-only synthetic memory only.\n", scenarios);
}
