// 补丁事务使用合成内存验证：不持有游戏文件、不附加游戏进程、不修改存档。
#include "patch_plan.h"
#include "unavailable_members.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace sky2party;
static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Memory {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x400000, 0xCC);
    unsigned exchanges = 0, failExchange = 0;
    bool unreadable = false;
    int foreignChange = 0;
    CodeEdit first;
    template<size_t N>
    explicit Memory(const std::array<CodeEdit, N>& plan) : first(plan[0]) {
        for (const auto& edit : plan)
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
            // 模拟安装过程中其它代码修改器改写早先的目标。回滚不能覆盖它。
            if (foreignChange == 1) bytes[first.rva + first.operand] = 0xA5;
            if (foreignChange == 2) bytes[first.rva] = 0x90;
            return false;
        }
        if (rva >= bytes.size() || bytes[rva] != expected) return false;
        bytes[rva] = replacement;
        return true;
    }
};

template<size_t N>
unsigned RunPlan(const std::array<CodeEdit, N>& plan) {
    unsigned cases = 0;
    {
        Memory memory(plan);
        const auto original = memory.bytes;
        Require(ApplyPlan(memory, plan, memory.bytes.size()).status == ApplyStatus::Applied, "valid edits");
        size_t changes = 0;
        for (size_t i = 0; i < original.size(); ++i) changes += memory.bytes[i] != original[i];
        Require(changes == plan.size(), "only the declared operand bytes may change");
        for (const auto& edit : plan) {
            Require(memory.bytes[edit.rva + edit.operand] == edit.replacement, "declared replacement applied");
            Require(UndoEdit(memory, edit), "successful rollback");
        }
        Require(memory.bytes == original, "round trip restores exact image");
        ++cases;
    }
    for (size_t site = 0; site < plan.size(); ++site) {
        Memory memory(plan);
        memory.bytes[plan[site].rva] ^= 1;
        const auto foreign = memory.bytes;
        const auto result = ApplyPlan(memory, plan, memory.bytes.size());
        Require(result.status == ApplyStatus::SignatureMismatch && result.index == site, "signature mismatch location");
        Require(!memory.exchanges && memory.bytes == foreign, "all sites checked before first write");
        ++cases;
    }
    for (unsigned failAt = 1; failAt <= plan.size(); ++failAt) {
        Memory memory(plan);
        const auto original = memory.bytes;
        memory.failExchange = failAt;
        Require(ApplyPlan(memory, plan, memory.bytes.size()).status == ApplyStatus::WriteConflict, "write conflict detected");
        Require(memory.bytes == original, "partial install rolled back");
        ++cases;
    }
    for (int foreign = 1; foreign <= 2; ++foreign) {
        Memory memory(plan);
        memory.failExchange = 3;
        memory.foreignChange = foreign;
        Require(ApplyPlan(memory, plan, memory.bytes.size()).status == ApplyStatus::RollbackConflict, "foreign rollback rejected");
        const auto& first = plan[0];
        Require(memory.bytes[first.rva + first.operand] == (foreign == 1 ? 0xA5 : 0), "foreign site operand untouched");
        Require(memory.bytes[plan[1].rva + plan[1].operand] == 1, "unaffected earlier site restored");
        ++cases;
    }
    {
        Memory memory(plan);
        memory.unreadable = true;
        Require(ApplyPlan(memory, plan, memory.bytes.size()).status == ApplyStatus::SignatureMismatch, "unreadable memory rejected");
        Require(memory.exchanges == 0, "no blind write");
        ++cases;
    }
    {
        Memory memory(plan);
        Require(ApplyPlan(memory, plan, memory.bytes.size()).status == ApplyStatus::Applied, "initial installation");
        const auto installed = memory.bytes;
        Require(ApplyPlan(memory, plan, memory.bytes.size()).status == ApplyStatus::SignatureMismatch, "already-modified code not blindly accepted");
        Require(memory.bytes == installed, "duplicate operation preserves first instance");
        ++cases;
    }
    {
        auto bad = plan;
        bad[1].rva = bad[0].rva + 1;
        Require(!ValidPlan(bad, 0x400000), "overlap rejected");
        bad = plan; bad[0].length = 17;
        Require(!ValidPlan(bad, 0x400000), "oversized signature rejected");
        bad = plan; bad[0].operand = bad[0].length;
        Require(!ValidPlan(bad, 0x400000), "invalid operand rejected");
        Require(!ValidPlan(plan, 0x139134), "out-of-image write rejected");
        ++cases;
    }
    return cases;
}

int main() {
    unsigned cases = RunPlan(kPartyEdits) + RunPlan(kUnlockedPartyEdits);
    // 关闭角色解锁时，仍使用相同游戏镜像；基础计划不得改动任一新增条件。
    Memory disabled(kUnlockedPartyEdits);
    Require(ApplyPlan(disabled, kPartyEdits, disabled.bytes.size()).status == ApplyStatus::Applied,
            "base features install with unlocking disabled");
    for (const auto& edit : kUnavailableMemberEdits)
        Require(std::memcmp(disabled.bytes.data()+edit.rva, edit.expected.data(), edit.length)==0,
                "disabled unlocking preserves every native availability instruction");
    ++cases;
    std::printf("PASS: %u transaction scenarios across both configurations; signatures, rollback and disabled scope verified.\n", cases);
}
