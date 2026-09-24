// 使用假原生API验证菜单操作顺序、工厂失败和重复输入；不调用游戏函数。
#include "menu_route.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Api {
    sky2party::MenuScope scope{true,true,true,1,0,0,false};
    bool original=false, allowed=true, pressed=true, factory=true, childExists=false;
    uint32_t uiFlags=0xA5A50003;
    std::string calls;
    bool OriginalHandled() { calls+='O';return original; }
    bool Capture() { calls+='C';return !childExists && sky2party::CanRouteFormation(scope); }
    bool Allowed() { calls+='A';return allowed; }
    bool Pressed() { calls+='P';return pressed; }
    bool OpenParty() { calls+='N';childExists=factory;return factory; }
    void RecreateTopOnReturn() { calls+='R';uiFlags=sky2party::RecreateTopFlags(uiFlags); }
    void WaitForParty() { calls+='W'; }
};
int main() {
    unsigned cases=0;
    {
        Api a;
        Require(sky2party::RouteFormation(a) && a.calls=="OCAPNRW","open, retire old UI and wait in native order");
        Require(a.uiFlags==0xA5A50002,"preserve all UI bits except retained-instance bit");
        a.calls.clear();
        Require(!sky2party::RouteFormation(a) && a.calls=="OC","same held input cannot open duplicate PartyMenu");
        ++cases;
    }
    {
        Api a;a.original=true;
        Require(sky2party::RouteFormation(a) && a.calls=="O","other native menu action has priority");++cases;
    }
    for (int scenario=0; scenario<7; ++scenario) {
        Api a;
        switch (scenario) {
        case 0:a.scope.ownedByField=false;break; // 已不是活动主菜单。
        case 1:a.scope.validObjects=false;break; // 对象身份不匹配。
        case 2:a.scope.currentParty=false;break; // 剧情已切换当前分组。
        case 3:a.scope.menuKind=13;break; // 已打开协会编成或其他专用入口。
        case 4:a.scope.environment=2;break; // 战斗/事件环境不能当作自由探索。
        case 5:a.scope.transition=1;break; // 换图请求未完成。
        case 6:a.scope.storyBlocked=true;break; // 游戏剧情明确禁止编成。
        }
        Require(!sky2party::RouteFormation(a) && a.calls=="OC","blocked context has no UI side effects");++cases;
    }
    {
        Api a;a.allowed=false;
        Require(!sky2party::RouteFormation(a) && a.calls=="OCA","keep native action restriction");++cases;
    }
    {
        Api a;a.pressed=false;
        Require(!sky2party::RouteFormation(a) && a.calls=="OCAP","no request does not open menu");++cases;
    }
    {
        Api a;a.factory=false;
        Require(!sky2party::RouteFormation(a) && a.calls=="OCAPN" && a.uiFlags==0xA5A50003,
            "factory failure must not retire main UI or change parent state");++cases;
    }
    std::printf("PASS: %u menu routing and lifecycle scenarios.\n",cases);
}
