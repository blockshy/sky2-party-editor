// 已审计的编成菜单及探索入口代码修改，不改队伍数据或出战人数。
// 保留原生输入、交换、排序和剧情执行逻辑，仅调整明确的条件操作数。
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sky2party {
struct CodeEdit {
    uint32_t rva;
    std::array<uint8_t, 16> expected;
    uint8_t length;
    uint8_t operand;
    uint8_t replacement;
    const char* name;
};

// 0x20 经右移5位成为 TEST 的低位。把掩码1改成0只使该处“固定主力”
// 条件不成立；不清除真实 flags，不放宽0x80/0x800、成员有效性或四人上限。
inline constexpr std::array<CodeEdit, 4> kFixedMemberEdits{{
    {0x13912F, {0xC1,0xEA,0x05,0xF6,0xC2,0x01,0x74,0x18}, 8,5,0,
        "swap-first-fixed-member"},
    {0x139178, {0xC1,0xEA,0x05,0xF6,0xC2,0x01,0x74,0x13}, 8,5,0,
        "swap-second-fixed-member"},
    {0x1DE75D, {0x41,0xC1,0xE9,0x05,0x41,0xF6,0xC1,0x01,0x75,0x0B}, 10,7,0,
        "list-item-selectability"},
    {0x1DFE6C, {0xC1,0xEA,0x05,0xF6,0xC2,0x01,0x0F,0x85,0x48,0x02,0x00,0x00}, 12,5,0,
        "reserve-list-fixed-member-visibility"}
}};

// 探索画面读取的是原生“打开队伍”操作75（Xbox当前为X）。场景运行时元数据的0x800位
// 原本把入口限定在协会等区域。将短JE的位移30改为00，使两种地点都落入后续
// 原生校验；不改地图属性、真实剧情旗标或输入状态，空地图指针仍由前置检查拒绝。
inline constexpr CodeEdit kFieldEntryEdit{
    0x2D50E8, {0xF7,0x40,0x58,0x00,0x08,0x00,0x00,0x74,0x30,0x48,0x8B,0x05,0x60,0xBD,0x98,0x00},
    16,8,0,"field-party-location-gate"
};
// 原生party_menu_icon_in提示的出现和保持使用同一地点规则。同步放宽两个
// 地点分支，按键图标与输入方式由游戏处理；119的入口规则由下方独立计划同步处理。
inline constexpr std::array<CodeEdit,3> kFieldLocationEdits{{
    kFieldEntryEdit,
    {0x9F916, {0xF7,0x41,0x58,0x00,0x08,0x00,0x00,0x74,0x44,0x48,0x8B,0x05,0x32,0x15,0xBC,0x00},
        16,8,0,"field-party-hint-show-location-gate"},
    {0x9FB34, {0xF7,0x40,0x58,0x00,0x08,0x00,0x00,0x74,0x10,0x48,0x8B,0x05,0x14,0x13,0xBC,0x00},
        16,8,0,"field-party-hint-keep-location-gate"}
}};
// 系统旗标119在本构建的这三处被编译为“CMP有符号字节,0; JL/JGE”。这三处仅影响
// 编成入口及对应HUD，部分可自由探索的进度也可能置位。随处编成开启时，
// 把比较立即数改为有符号下限-128，使JL恒不成立、JGE恒成立；保持原跳转方向、
// 地址、输入优先级和实际剧情字节。严格限定这三处，不全局清119或跳过剧情流程。
// 角色选择中的系统旗标30、战斗/演出、Camp生命周期与换图门槛不在此计划中。
inline constexpr std::array<CodeEdit,3> kFieldEntry119Edits{{
    {0x2D50F8, {0x80,0xB8,0x0E,0x01,0x00,0x00,0x00,0x7C,0x20},
        9,6,0x80,"field-party-entry119-gate"},
    {0x9F926, {0x80,0xB8,0x0E,0x01,0x00,0x00,0x00,0x7C,0x34},
        9,6,0x80,"field-party-hint-show-entry119-gate"},
    {0x9FB44, {0x80,0xB8,0x0E,0x01,0x00,0x00,0x00,0x7D,0x34},
        9,6,0x80,"field-party-hint-keep-entry119-gate"}
}};
inline constexpr std::array<CodeEdit,10> kPartyEdits{{
    kFixedMemberEdits[0],kFixedMemberEdits[1],kFixedMemberEdits[2],kFixedMemberEdits[3],
    kFieldLocationEdits[0],kFieldLocationEdits[1],kFieldLocationEdits[2],
    kFieldEntry119Edits[0],kFieldEntry119Edits[1],kFieldEntry119Edits[2]
}};

enum class ApplyStatus { Applied, InvalidPlan, SignatureMismatch, WriteConflict, RollbackConflict };
struct ApplyResult { ApplyStatus status; size_t index; };

template<size_t N>
bool ValidPlan(const std::array<CodeEdit, N>& plan, size_t imageSize) noexcept {
    if (!N || N > 64) return false;
    for (size_t i = 0; i < N; ++i) {
        const auto& edit = plan[i];
        if (!edit.length || edit.length > edit.expected.size() || edit.operand >= edit.length ||
            edit.expected[edit.operand] == edit.replacement || edit.rva > imageSize ||
            edit.length > imageSize - edit.rva) return false;
        // 签名区间也不能重叠，保证后续签名复查不把本 Mod 自己的修改识别为冲突。
        for (size_t j = 0; j < i; ++j)
            if (static_cast<size_t>(edit.rva) < static_cast<size_t>(plan[j].rva) + plan[j].length &&
                static_cast<size_t>(plan[j].rva) < static_cast<size_t>(edit.rva) + edit.length) return false;
    }
    return true;
}

template<class Memory, size_t N>
bool MatchesPlan(Memory& memory, const std::array<CodeEdit, N>& plan, size_t& badIndex) noexcept {
    std::array<uint8_t, 16> bytes{};
    for (size_t i = 0; i < N; ++i) {
        const auto& edit = plan[i];
        if (!memory.Read(edit.rva, bytes.data(), edit.length) ||
            std::memcmp(bytes.data(), edit.expected.data(), edit.length)) { badIndex = i; return false; }
    }
    return true;
}

template<class Memory>
bool UndoEdit(Memory& memory, const CodeEdit& edit) noexcept {
    auto expected = edit.expected;
    expected[edit.operand] = edit.replacement;
    std::array<uint8_t, 16> bytes{};
    // 回滚同样核对整条签名；其他 Mod 可能修改了跳转或寄存器操作码而保留0。
    // 这种情况不能仅凭操作数仍为0就擅自恢复原字节，必须保留外部修改并报告冲突。
    return memory.Read(edit.rva, bytes.data(), edit.length) &&
        std::memcmp(bytes.data(), expected.data(), edit.length) == 0 &&
        memory.Exchange(edit.rva + edit.operand, edit.replacement, edit.expected[edit.operand]);
}

// 写入方先使对应代码页可写并负责恢复保护/刷新指令缓存。事务只修改各单字节
// 立即数；比较交换防止覆盖并发改写，失败时只撤回仍等于本 Mod 新值的字节。
template<class Memory, size_t N>
ApplyResult ApplyPlan(Memory& memory, const std::array<CodeEdit, N>& plan, size_t imageSize) noexcept {
    if (!ValidPlan(plan, imageSize)) return {ApplyStatus::InvalidPlan, 0};
    size_t badIndex = 0;
    if (!MatchesPlan(memory, plan, badIndex)) return {ApplyStatus::SignatureMismatch, badIndex};
    std::array<uint8_t, 16> bytes{};
    for (size_t i = 0; i < N; ++i) {
        const auto& edit = plan[i];
        // 全部预检后仍逐项复查；不能只验证立即数字节而忽略同一指令已被替换。
        const bool matches = memory.Read(edit.rva, bytes.data(), edit.length) &&
            std::memcmp(bytes.data(), edit.expected.data(), edit.length) == 0;
        if (matches && memory.Exchange(edit.rva + edit.operand, edit.expected[edit.operand], edit.replacement))
            continue;
        bool restored = true;
        for (size_t j = i; j > 0; --j) {
            const auto& previous = plan[j - 1];
            restored &= UndoEdit(memory, previous);
        }
        return {restored ? ApplyStatus::WriteConflict : ApplyStatus::RollbackConflict, i};
    }
    return {ApplyStatus::Applied, N};
}
}
