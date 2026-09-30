// 队伍业务页只使用显式传入的本地 UI 函数表，不持有图形后端或输入挂钩。
// 窗口外壳负责语言刷新、导航和生命周期；此层只提交既有安全服务请求。
#pragma once
#include "standalone_ui/sky2_ui.h"

namespace sky2party {
void TickPartyPage(const Sky2Frame& frame) noexcept;
// 固定 Header 绘制页签，Main 根据 header_drawn 避免重复绘制。
void DrawPartyHeader(const Sky2UiApi& ui, const Sky2Frame& frame) noexcept;
void DrawPartyPage(const Sky2UiApi& ui, const Sky2Frame& frame) noexcept;
// 关闭、切侧栏或失焦时立即撤销尚未提交的角色确认。
void PartyPageVisibilityChanged(bool active) noexcept;
}
