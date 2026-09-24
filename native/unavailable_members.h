// 已登记角色的可选增强：只跳过菜单中的 0x80 暂时不可编成判断。
// 不添加名单条目、不初始化角色、不清除实际成员 flags，也不改变四人主力上限。
#pragma once
#include "patch_plan.h"

namespace sky2party {
// 五处均先把成员 flags 右移 7 位，再执行 TEST DL, 1。把该 TEST 的
// 单字节立即数改成 0，仅使此处的 0x80 条件不成立；分支、寄存器和成员数据不变。
// 原生 0x800 隐藏检查、低位角色分类、成员存在检查、剧情全局禁止和容量判断
// 均在各自指令中执行，不在本计划的修改范围内。尤其不能将 0x40 后备位一并清除。
inline constexpr std::array<CodeEdit, 5> kUnavailableMemberEdits{{
    // CanChangeMember：上游先验证成员存在并拒绝 0x800，最后以 SETE 返回 0x80 检查。
    {0x13906D, {0xC1,0xEA,0x07,0xF6,0xC2,0x01,0x0F,0x94,0xC0,0xC3},
        10,5,0,"registered-member-selectability"},
    // CanExchangeMembers：两个方向各有一次 0x80 判断，必须同时处理才可来回互换。
    {0x139294, {0xC1,0xEA,0x07,0xF6,0xC2,0x01,0x0F,0x85,0xD4,0x00,0x00,0x00},
        12,5,0,"registered-member-swap-first-direction"},
    {0x139364, {0xC1,0xEA,0x07,0xF6,0xC2,0x01,0x75,0x08},
        8,5,0,"registered-member-swap-second-direction"},
    // 后备卡片构建：原分支会设置卡片 +0x59 禁用状态；只放宽选择函数会留下灰置卡片。
    {0x1DFFC4, {0xC1,0xEA,0x07,0xF6,0xC2,0x01,0x74,0x25,0xC6,0x46,0x59,0x01},
        12,5,0,"registered-member-reserve-card-availability"},
    // PartyMenu 空槽选择路径有独立的同位前置判断；后续仍调用完整原生有效性查询。
    // 这不会绕过原生新增/移动接口的固定主力限制，也不会改变空槽或人数规则。
    {0x1E9762, {0xC1,0xEA,0x07,0xF6,0xC2,0x01,0x74,0x9C},
        8,5,0,"registered-member-empty-slot-selectability"}
}};

// 开启增强时与基础十处修改作为同一事务校验和提交，避免只解除灰置而未解除互换
// 门槛等部分应用状态。配置关闭时调用方继续使用基础 kPartyEdits，不应用这五处。
inline constexpr std::array<CodeEdit, 15> kUnlockedPartyEdits{{
    kPartyEdits[0], kPartyEdits[1], kPartyEdits[2], kPartyEdits[3],
    kPartyEdits[4], kPartyEdits[5], kPartyEdits[6],
    kPartyEdits[7], kPartyEdits[8], kPartyEdits[9],
    kUnavailableMemberEdits[0], kUnavailableMemberEdits[1], kUnavailableMemberEdits[2],
    kUnavailableMemberEdits[3], kUnavailableMemberEdits[4]
}};
}
