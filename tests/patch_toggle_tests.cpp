// 可逆开关事务的合成内存回归，不加载游戏文件、进程或存档。
// 覆盖三组独立开关的全部状态互转、每一步失败、外国修改及指令缓存刷新失败。
#include "feature_state.h"
#include "patch_toggle_policy.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace sky2party;
static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static NativeFeatureState State(unsigned mask) {
    return {(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
}

struct Memory {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x400000, 0xCC);
    unsigned exchanges = 0, failExchange = 0, flushes = 0, failFlush = 0;
    bool unreadable = false;
    // 在一次模拟 CAS 失败时修改更早的目标；0不干预，1改立即数，2改指令操作码。
    unsigned foreignChange = 0;
    size_t foreignSite = 0;
    Memory() {
        for (const auto& edit : kUnlockedPartyEdits)
            std::copy_n(edit.expected.begin(), edit.length, bytes.begin() + edit.rva);
    }
    bool Read(uint32_t rva, void* output, size_t length) noexcept {
        if (unreadable || rva > bytes.size() || length > bytes.size() - rva) return false;
        std::memcpy(output, bytes.data() + rva, length);
        return true;
    }
    bool Exchange(uint32_t rva, uint8_t expected, uint8_t replacement) noexcept {
        ++exchanges;
        if (exchanges == failExchange) {
            const auto& other = kUnlockedPartyEdits[foreignSite];
            if (foreignChange == 1) bytes[other.rva + other.operand] = 0xA5;
            if (foreignChange == 2) bytes[other.rva] = 0x90;
            return false;
        }
        if (rva >= bytes.size() || bytes[rva] != expected) return false;
        bytes[rva] = replacement;
        return true;
    }
    bool Flush() noexcept { return ++flushes != failFlush; }
};

using Ownership = PatchOwnership<kUnlockedPartyEdits.size()>;
static ToggleResult Apply(Memory& memory, Ownership& ownership, unsigned mask) {
    return ToggleOwnedPlan(memory, kUnlockedPartyEdits, ownership, NativeFeatureSites(State(mask)), memory.bytes.size());
}
static void RequireState(const Memory& memory, const Ownership& ownership, unsigned mask) {
    const auto expectedSites = NativeFeatureSites(State(mask));
    Require(ownership.owned == expectedSites, "ownership must describe the complete desired state");
    for (size_t i = 0; i < kUnlockedPartyEdits.size(); ++i) {
        auto expected = kUnlockedPartyEdits[i].expected;
        if (expectedSites[i]) expected[kUnlockedPartyEdits[i].operand] = kUnlockedPartyEdits[i].replacement;
        Require(std::memcmp(memory.bytes.data() + kUnlockedPartyEdits[i].rva, expected.data(),
                            kUnlockedPartyEdits[i].length) == 0, "complete instruction signatures match state");
    }
}

int main() {
    unsigned cases = 0;
    for (unsigned from = 0; from < 8; ++from) {
        for (unsigned to = 0; to < 8; ++to) {
            Memory memory;
            Ownership owned;
            Require(Apply(memory, owned, from).status == ToggleStatus::Applied, "establish source state");
            const auto sourceBytes = memory.bytes;
            memory.exchanges = memory.flushes = 0;
            Require(Apply(memory, owned, to).status == ToggleStatus::Applied, "all feature-state transitions apply");
            RequireState(memory, owned, to);
            if (from == to) Require(!memory.exchanges && !memory.flushes, "unchanged request must not write or flush");
            Require(Apply(memory, owned, from).status == ToggleStatus::Applied, "all feature-state transitions reverse");
            Require(memory.bytes == sourceBytes, "reverse preserves the complete original source image");
            ++cases;

            const auto a = NativeFeatureSites(State(from)), b = NativeFeatureSites(State(to));
            unsigned changes = 0;
            for (size_t i = 0; i < a.size(); ++i) changes += a[i] != b[i];
            // 包括“开一个组同时关另一个组”，每个中途 CAS 失败必须撤销整个请求。
            for (unsigned failAt = 1; failAt <= changes; ++failAt) {
                Memory failing;
                Ownership ownership;
                Require(Apply(failing, ownership, from).status == ToggleStatus::Applied, "prepare failure source");
                const auto before = failing.bytes;
                failing.exchanges = 0;
                failing.failExchange = failAt;
                Require(Apply(failing, ownership, to).status == ToggleStatus::WriteConflict, "failed write reports conflict");
                Require(failing.bytes == before, "failed mixed toggle restores every changed byte");
                RequireState(failing, ownership, from);
                ++cases;
            }
        }
    }
    for (unsigned from : {0u, 7u}) {
        for (size_t site = 0; site < kUnlockedPartyEdits.size(); ++site) {
            for (bool operand : {false, true}) {
                Memory memory;
                Ownership owned;
                Require(Apply(memory, owned, from).status == ToggleStatus::Applied, "prepare foreign-change state");
                const auto& edit = kUnlockedPartyEdits[site];
                memory.bytes[edit.rva + (operand ? edit.operand : 0)] = 0xA5;
                const auto foreign = memory.bytes;
                memory.exchanges = 0;
                const auto result = Apply(memory, owned, from ^ 7);
                Require(result.status == ToggleStatus::SignatureMismatch && result.index == site,
                        "foreign original or owned-patched signature rejected");
                Require(!memory.exchanges && memory.bytes == foreign, "foreign byte must never be overwritten");
                ++cases;
            }
        }
        for (unsigned foreign : {1u, 2u}) {
            Memory memory;
            Ownership owned;
            Require(Apply(memory, owned, from).status == ToggleStatus::Applied, "prepare rollback-conflict state");
            memory.exchanges = 0;
            memory.failExchange = 3;
            memory.foreignChange = foreign;
            Require(Apply(memory, owned, from ^ 7).status == ToggleStatus::RollbackConflict,
                    "foreign modification during rollback reported");
            const auto& edit = kUnlockedPartyEdits[0];
            Require(memory.bytes[edit.rva + (foreign == 1 ? edit.operand : 0)] == (foreign == 1 ? 0xA5 : 0x90),
                    "rollback retains foreign operand or opcode");
            Require(owned.owned[0] != ((from & 1) != 0), "ownership does not falsely claim successful rollback");
            Require(owned.owned[1] == ((from & 1) != 0), "unaffected earlier site rolled back");
            ++cases;
        }
        {
            Memory memory;
            Ownership owned;
            Require(Apply(memory, owned, from).status == ToggleStatus::Applied, "prepare flush-failure state");
            const auto before = memory.bytes;
            memory.flushes = 0;
            memory.failFlush = 1;
            Require(Apply(memory, owned, from ^ 7).status == ToggleStatus::FlushFailed, "failed cache flush is not success");
            Require(memory.bytes == before && memory.flushes == 2, "flush failure rolls back and flushes restored bytes");
            RequireState(memory, owned, from);
            ++cases;
        }
    }
    {
        Memory memory;
        Ownership owned;
        // 别的实例事先写入了完全相同的补丁，也不能在没有 owned 记录时接管或还原。
        const auto& edit = kUnlockedPartyEdits[0];
        memory.bytes[edit.rva + edit.operand] = edit.replacement;
        const auto foreign = memory.bytes;
        Require(Apply(memory, owned, 7).status == ToggleStatus::SignatureMismatch, "identical foreign patch not adopted");
        Require(memory.bytes == foreign && !memory.exchanges, "identical foreign patch retained");
        ++cases;
    }
    {
        Memory memory;
        Ownership owned;
        memory.unreadable = true;
        Require(Apply(memory, owned, 7).status == ToggleStatus::SignatureMismatch && !memory.exchanges,
                "unreadable code cannot be modified");
        ++cases;
    }
    {
        Memory memory;
        Ownership owned;
        auto invalid = kUnlockedPartyEdits;
        invalid[1].rva = invalid[0].rva;
        Require(ToggleOwnedPlan(memory, invalid, owned, NativeFeatureSites(State(7)), memory.bytes.size()).status ==
                ToggleStatus::InvalidPlan, "overlapping plan cannot toggle");
        Require(!memory.exchanges, "invalid plan has no side effects");
        ++cases;
    }
    std::printf("PASS: %u reversible feature-toggle scenarios; ownership, disabled groups, rollback and cache flush verified.\n", cases);
}
