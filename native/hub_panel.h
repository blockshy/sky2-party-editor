// Hub 页面只通过公共 UI 函数表绘制，不包含 ImGui、DXGI 或输入挂钩。
#pragma once
#include "sky2_hub.h"

namespace sky2party {
// 注册稳定动作 ID；角色操作动作只打开页面，真实写入仍需逐人二次确认。
bool RegisterHubActions() noexcept;
void SKY2_CALL TickHubPanel(const Sky2Frame* frame) noexcept;
// 顶部页签由新宿主放入固定 Header；旧宿主仍从 DrawHubPanel 内联绘制。
void SKY2_CALL DrawHubHeader(const Sky2Frame* frame) noexcept;
void SKY2_CALL DrawHubPanel(const Sky2Frame* frame) noexcept;
void SKY2_CALL HubVisibilityChanged(int32_t active) noexcept;
}
