// 可逆代码开关的纯事务策略；不依赖 Windows、游戏地址或实际页保护。
// 调用方负责串行化、选择安全游戏帧、准备写权限，Memory 负责单字节比较交换及刷新。
#pragma once
#include "patch_plan.h"

namespace sky2party {
enum class ToggleStatus { Applied, InvalidPlan, SignatureMismatch, WriteConflict, RollbackConflict, FlushFailed };
struct ToggleResult { ToggleStatus status; size_t index; };

template<size_t N>
struct PatchOwnership {
    // true 只表示该点最后一次成功写入由本实例完成。每次使用前仍核验完整签名，
    // 不能因为字节数值恰好相同就接管启动前已被其他修改器改写的代码。
    std::array<bool, N> owned{};
};

template<class Memory>
bool MatchesOwnedEdit(Memory& memory, const CodeEdit& edit, bool owned) noexcept {
    auto expected = edit.expected;
    if (owned) expected[edit.operand] = edit.replacement;
    std::array<uint8_t, 16> actual{};
    return memory.Read(edit.rva, actual.data(), edit.length) &&
        std::memcmp(actual.data(), expected.data(), edit.length) == 0;
}

template<class Memory, size_t N>
bool MatchesOwnedPlan(Memory& memory, const std::array<CodeEdit, N>& plan,
                      const PatchOwnership<N>& ownership, size_t& badIndex) noexcept {
    for (size_t i = 0; i < N; ++i)
        if (!MatchesOwnedEdit(memory, plan[i], ownership.owned[i])) { badIndex = i; return false; }
    return true;
}

template<class Memory, size_t N>
ToggleResult ToggleOwnedPlan(Memory& memory, const std::array<CodeEdit, N>& plan,
                            PatchOwnership<N>& ownership, const std::array<bool, N>& desired,
                            size_t imageSize) noexcept {
    if (!ValidPlan(plan, imageSize)) return {ToggleStatus::InvalidPlan, 0};
    size_t badIndex = 0;
    // 连未变化的组也预检，确保请求是针对一致、可信的整套实际状态提交的。
    if (!MatchesOwnedPlan(memory, plan, ownership, badIndex))
        return {ToggleStatus::SignatureMismatch, badIndex};
    const auto previous = ownership.owned;
    std::array<size_t, N> changed{};
    size_t changedCount = 0;
    auto rollback = [&]() noexcept {
        bool restored = true;
        for (size_t at = changedCount; at > 0; --at) {
            const size_t i = changed[at - 1];
            const auto& edit = plan[i];
            const auto currentByte = ownership.owned[i] ? edit.replacement : edit.expected[edit.operand];
            const auto previousByte = previous[i] ? edit.replacement : edit.expected[edit.operand];
            // 包括指令非操作数字节在内都必须仍为本事务的预期；否则保留外国修改。
            if (MatchesOwnedEdit(memory, edit, ownership.owned[i]) &&
                memory.Exchange(edit.rva + edit.operand, currentByte, previousByte))
                ownership.owned[i] = previous[i];
            else restored = false;
        }
        return restored;
    };
    for (size_t i = 0; i < N; ++i) {
        if (desired[i] == previous[i]) continue;
        const auto& edit = plan[i];
        const auto oldByte = previous[i] ? edit.replacement : edit.expected[edit.operand];
        const auto newByte = desired[i] ? edit.replacement : edit.expected[edit.operand];
        // 全量预检后逐点复查，缩小外部修改器在两次检查之间更改代码的竞争窗口。
        if (!MatchesOwnedEdit(memory, edit, previous[i]) ||
            !memory.Exchange(edit.rva + edit.operand, oldByte, newByte)) {
            const bool restored = rollback();
            const bool flushed = changedCount == 0 || memory.Flush();
            if (!restored) return {ToggleStatus::RollbackConflict, i};
            return {flushed ? ToggleStatus::WriteConflict : ToggleStatus::FlushFailed, i};
        }
        ownership.owned[i] = desired[i];
        changed[changedCount++] = i;
    }
    if (changedCount && !memory.Flush()) {
        const bool restored = rollback();
        // 即使回滚遇到外国修改也刷新自己已成功恢复的代码；不重试任何外国字节。
        memory.Flush();
        return {restored ? ToggleStatus::FlushFailed : ToggleStatus::RollbackConflict, N};
    }
    return {ToggleStatus::Applied, N};
}
}
