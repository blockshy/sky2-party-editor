// 加入前仅补全已有培养记录的缺失基础武器/空战技，不创建 FFFF 空白角色记录。
// 此功能只能附属于用户逐人确认的加入操作，不能用于每帧自动送装备。
// 空战技列表允许保留已核验的本人 S 战技选择；补足时仅恢复这一个现存选择，
// 其余技能由原生按等级学习。未知/他人 S 或超等级正学习技能不会被强行补回。
#pragma once
#include "roster_policy.h"

namespace sky2party {
enum class RosterPreparationResult { NotNeeded, Prepared, Unsupported, InvalidData, Failed, PartialPrepared };

// 原版 t_item 装备角色列表已核验。共享武器系的客串选基础款，不照搬后期剧情的
// 高阶装备模板；克鲁茨只有专属长枪950。数值只是已有物品ID，不包含原版资源。
constexpr uint32_t DefaultRosterWeapon(uint32_t id) noexcept {
    switch (id) {
    case 0: return 500;
    case 1: return 540;
    case 2: return 580;
    case 3: case 106: return 620;
    case 4: case 107: return 660;
    case 5: case 112: return 700;
    case 6: return 740;
    case 7: return 780;
    case 119: return 900;
    case 100: return 950;
    case 101: return 940;
    default: return 0;
    }
}
constexpr bool CanPrepareRosterRecord(const RosterRecordFacts& facts) noexcept {
    // 武器0为已审核的离队卸装空槽；FFFF/FFFFFFFF表示未知状态，绝不能当空槽覆盖。
    return HasRosterCoreData(facts) && DefaultRosterWeapon(facts.requestedId) != 0 &&
        facts.weapon != 0xFFFF && facts.weapon != kNoRosterId &&
        (facts.weapon == 0 || !facts.hasCraft);
}

bool ConfigureRosterPreparation(uintptr_t executableBase) noexcept;
// 调用前服务必须确认自由探索、安全场景、名单容量以及确实要新增/显示后备。
// 已可见的成员须先返回 AlreadyPresent，不因卸下武器而重复触发配装。
// PartialPrepared 表示已发生部分原生培养变更，但未完整成功；调用方必须停止加入
// 并如实显示，不得声称已回滚，也不得自动重试或复制整条存档结构“恢复”。
RosterPreparationResult PrepareRosterRecordOnGameThread(uint32_t id, uintptr_t exactRecord) noexcept;
}
