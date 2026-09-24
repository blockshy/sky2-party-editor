// 角色补缺的授权范围回归：只测试纯规则，不装载或调用任何游戏函数。
#include "roster_prepare.h"
#include <cstdio>
#include <cstdlib>

using namespace sky2party;
static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    unsigned scenarios = 0;
    for (const auto& role : kRosterDefinitions) {
        RosterRecordFacts valid{role.id,role.id,50,1000,100,200,0,false,false};
        Require(DefaultRosterWeapon(role.id) != 0 && CanPrepareRosterRecord(valid), "allow supported cultivated role with missing items");
        valid.weapon = DefaultRosterWeapon(role.id);
        Require(CanPrepareRosterRecord(valid), "empty craft alone can be prepared");
        valid.hasCraft = true;
        Require(!CanPrepareRosterRecord(valid), "complete role needs no preparation");
        valid.weapon = 0;
        Require(CanPrepareRosterRecord(valid), "missing weapon alone can be prepared");
        for (const uint32_t invalidWeapon : {0xFFFFu, 0xFFFFFFFFu}) {
            valid.weapon = invalidWeapon;
            Require(!CanPrepareRosterRecord(valid), "unknown weapon sentinel must not be treated as an empty slot");
        }
        valid.weapon = 0;
        valid.storedId = 0xFFFF;
        Require(!CanPrepareRosterRecord(valid), "uninitialized fallback slot must not be modified");
        valid.storedId = role.id;
        valid.duplicate = true;
        Require(!CanPrepareRosterRecord(valid), "duplicate exact IDs are ambiguous");
        valid.duplicate = false;
        valid.level = 0;
        Require(!CanPrepareRosterRecord(valid), "blank level is not established cultivation");
        valid.level = 1000;
        Require(!CanPrepareRosterRecord(valid), "implausible level is rejected");
        valid.level = 50;
        valid.maxHp = 0;
        Require(!CanPrepareRosterRecord(valid), "missing core health cannot be repaired by equipment alone");
        scenarios += 11;
    }
    for (const uint32_t id : {8u, 102u, 158u, 0xFFFFu, 0xFFFFFFFFu}) {
        RosterRecordFacts npc{id,id,50,1000,100,200,0,false,false};
        Require(DefaultRosterWeapon(id) == 0 && !CanPrepareRosterRecord(npc), "arbitrary NPC cannot receive a player template");
        ++scenarios;
    }
    // 客串的共享武器系来自物品表适配列表，避免误赠其后期剧情的高阶武器。
    Require(DefaultRosterWeapon(106) == 620 && DefaultRosterWeapon(107) == 660 &&
        DefaultRosterWeapon(112) == 700 && DefaultRosterWeapon(100) == 950, "guest baseline weapons remain conservative");
    ++scenarios;
    std::printf("PASS: %u roster missing-data policy scenarios; existing cultivation and unknown records remain protected.\n", scenarios);
}
