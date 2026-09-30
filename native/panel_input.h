// 面板输入接入层：合作游戏 IAT 读取同一映射设备样本，仅最外层负责最终吞键。
#pragma once
#include <Windows.h>
#include <cstdint>
#include "panel_input_policy.h"
namespace sky2party {
using PanelWindowMessage = void (*)(HWND, UINT, WPARAM, LPARAM) noexcept;
bool InstallPanelInput(uintptr_t executableBase) noexcept;
void AttachPanelWindow(HWND window, PanelWindowMessage callback) noexcept;
void PumpPanelKeyboard() noexcept;
uint32_t ConsumePanelActions() noexcept;
bool PanelOpen() noexcept;
void SetPanelOpen(bool open) noexcept;
// 可见性与输入所有权分离；后台窗口保留，只有健康前台帧允许交互。
bool PanelInteractive() noexcept;
void SetPanelFrameHealth(bool healthy) noexcept;
// 只由 Present 消费，用于在失焦、渲染失败后取消业务确认并清理 GUI 输入。
bool ConsumePanelReset() noexcept;
// 返回游戏原有 XInput 调用链观察到的单一设备样本，不另行轮询物理设备。
bool ReadPanelPad(panelinput::Pad& sample) noexcept;
bool PanelUsingController() noexcept;
bool PanelControllerReady() noexcept;
const char* PanelInputStatus() noexcept;
}
