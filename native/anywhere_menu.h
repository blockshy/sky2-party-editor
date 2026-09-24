// 主菜单挂钩分为预检、创建和启用；与固定队员及探索入口的代码修改一起完成初始化。
#pragma once
#include <cstdint>
namespace sky2party {
bool PrepareAnywhereMenu(uintptr_t base) noexcept;
bool EnableAnywhereMenu() noexcept;
void DiscardAnywhereMenu() noexcept;
// 只在地点字节事务成功后发布；面板不得直接修改，关闭时严格保留原生输入处理。
void SetAnywhereMenuEnabled(bool enabled) noexcept;
}
