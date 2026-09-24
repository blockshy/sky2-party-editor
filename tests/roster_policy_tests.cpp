// 这些测试只验证纯策略，覆盖误加 NPC、空白/重复状态、隐藏主力及过期请求。
// 不加载游戏、不访问存档，也不调用任何原生 Party 函数。
#include "roster_policy.h"
#include <cstdio>

using namespace sky2party;
namespace {
int g_checks = 0, g_failed = 0;
void Check(bool condition, const char* description) {
    ++g_checks;
    if (!condition) { ++g_failed; std::fprintf(stderr, "FAILED: %s\n", description); }
}
}
int main() {
    for (const auto& role : kRosterDefinitions) {
        Check(RosterIndex(role.id) < 14, "白名单角色可查找");
        RosterRecordFacts record{role.id, role.id, 31, 2000, 200, 200, 500, true, false};
        Check(IsInitializedRosterRecord(record), "精确 ID 已培养记录可复用");
        record.storedId = 0xFFFF;
        Check(!IsInitializedRosterRecord(record), "非空占位槽不是已初始化角色");
        record.storedId = role.id;
        record.duplicate = true;
        Check(!IsInitializedRosterRecord(record), "重复精确状态 ID 必须拒绝");
    }
    for (uint32_t id : {8u, 9u, 102u, 110u, 120u, 0xFFFFu, kNoRosterId})
        Check(RosterIndex(id) == 14, "未核验 NPC/变体不因数值合法而进入名单");

    RosterRecordFacts record{4, 4, 79, 7000, 500, 200, 600, true, false};
    auto bad = record; bad.storedId = 3;
    Check(!IsInitializedRosterRecord(bad), "共享槽中的其他角色培养不可复用");
    bad = record; bad.level = 0;
    Check(!IsInitializedRosterRecord(bad), "未设置等级拒绝");
    bad = record; bad.maxHp = 0;
    Check(!IsInitializedRosterRecord(bad), "未计算 HP 拒绝");
    bad = record; bad.maxEp = 0;
    Check(!IsInitializedRosterRecord(bad), "未计算 EP 拒绝");
    bad = record; bad.maxCp = 0;
    Check(!IsInitializedRosterRecord(bad), "未计算 CP 拒绝");
    bad = record; bad.weapon = 0;
    Check(!IsInitializedRosterRecord(bad), "空武器拒绝");
    Check(HasRosterCoreData(bad), "空武器和空核心状态须分开，由补足策略独立判断");
    bad = record; bad.weapon = 0xFFFF;
    Check(!IsInitializedRosterRecord(bad), "无效武器占位拒绝");
    bad = record; bad.hasCraft = false;
    Check(!IsInitializedRosterRecord(bad), "空战技列表拒绝");

    RosterAddFacts add{4, 10, 0, true, false, true};
    Check(PlanRosterAdd(add) == RosterResult::AddedReserve, "已培养但未入队允许原生加入后备");
    add.count = 64;
    Check(PlanRosterAdd(add) == RosterResult::Full, "64 人容量不可溢出");
    add.count = 10; add.inParty = true; add.memberFlags = 0x20;
    Check(PlanRosterAdd(add) == RosterResult::AlreadyPresent, "可见固定主力不被按钮改成后备");
    add.memberFlags = 0xC0;
    Check(PlanRosterAdd(add) == RosterResult::AlreadyPresent, "已有暂禁后备由菜单开关负责");
    add.initialized = false;
    Check(PlanRosterAdd(add) == RosterResult::AlreadyPresent, "可见角色卸武器后不能借加入按钮反复补物品");
    add.initialized = true;
    add.memberFlags = 0x8C0;
    Check(PlanRosterAdd(add) == RosterResult::RevealedReserve, "显式请求可显示已有隐藏后备");
    Check(RevealedReserveFlags(0x8E0) == 0xE0, "清隐藏同时保留强制/后备/暂禁位");
    Check(RevealedReserveFlags(0x100008E0) == 0x100000E0, "未知高位保持不变");
    add.memberFlags = 0x820;
    Check(PlanRosterAdd(add) == RosterResult::InvalidData, "隐藏主力不会被隐式重新编成");
    for (uint32_t category : {1u, 2u, 8u}) {
        add.memberFlags = 0x840 | category;
        Check(PlanRosterAdd(add) == RosterResult::InvalidData, "特殊随行类别不作普通后备显示");
    }
    add.memberFlags = 0x840; add.initialized = false;
    Check(PlanRosterAdd(add) == RosterResult::Uninitialized, "隐藏空白角色也不能直接解锁");
    add.initialized = true; add.validParty = false;
    Check(PlanRosterAdd(add) == RosterResult::InvalidData, "无效队伍先拒绝");
    add.validParty = true; add.id = 110;
    Check(PlanRosterAdd(add) == RosterResult::UnsupportedCharacter, "直接调用也无法绕过白名单");

    Check(RosterRequestStillCurrent(1100, 1000, 3, 3), "同场景当前请求有效");
    Check(RosterRequestStillCurrent(3000, 1000, 3, 3), "有效期边界可预测");
    Check(!RosterRequestStillCurrent(3001, 1000, 3, 3), "过期不延迟自动加入");
    Check(!RosterRequestStillCurrent(1100, 1000, 3, 4), "换图读档导致的世代变化取消请求");
    Check(!RosterRequestStillCurrent(900, 1000, 3, 3), "异常时钟方向拒绝");
    Check(!RosterRequestStillCurrent(1100, 1000, 0, 0), "无首帧快照不可执行");
    std::printf("%d roster policy checks, %d failures\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
