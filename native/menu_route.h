// 主菜单到完整编成页的操作顺序。逻辑与游戏地址分离，以验证失败分支不改变UI。
#pragma once
#include <cstdint>

namespace sky2party {
struct MenuScope {
    bool ownedByField = false;
    bool validObjects = false;
    bool currentParty = false;
    uint32_t menuKind = 0;
    uint32_t environment = 0;
    uint32_t transition = 0;
    bool storyBlocked = true;
};

constexpr bool CanRouteFormation(const MenuScope& scope) noexcept {
    // 只扩展自由探索中由游戏合法打开的普通主菜单。战斗菜单、事件专用菜单、
    // 过期队伍和换图中的对象不能作为入口；不清除系统剧情禁用旗标30。
    return scope.ownedByField && scope.validObjects && scope.currentParty &&
        scope.menuKind == 1 && scope.environment == 0 && scope.transition == 0 && !scope.storyBlocked;
}

constexpr uint32_t RecreateTopFlags(uint32_t flags) noexcept { return flags & ~uint32_t{1}; }

template<class Api>
bool RouteFormation(Api& api) noexcept {
    // 其它主菜单项目优先由原函数处理，避免截走同帧已处理的菜单操作。
    if (api.OriginalHandled()) return true;
    if (!api.Capture() || !api.Allowed() || !api.Pressed()) return false;
    // 工厂失败时不关闭当前菜单、不改变状态；成功后才交给原生生命周期清理。
    if (!api.OpenParty()) return false;
    api.RecreateTopOnReturn();
    api.WaitForParty();
    return true;
}
}
