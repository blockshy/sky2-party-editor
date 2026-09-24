// 面板输入接入层：游戏 IAT 负责最终吞键，XInput 导出链负责读取同一映射设备的原始样本。
#pragma once
#include <Windows.h>
#include <cstdint>
namespace sky2party {
using PanelWindowMessage = void (*)(HWND, UINT, WPARAM, LPARAM) noexcept;
bool InstallPanelInput(uintptr_t executableBase) noexcept;
void AttachPanelWindow(HWND window, PanelWindowMessage callback) noexcept;
void PumpPanelKeyboard() noexcept;
uint32_t ConsumePanelActions() noexcept;
bool PanelOpen() noexcept;
void SetPanelOpen(bool open) noexcept;
bool PanelUsingController() noexcept;
bool PanelControllerReady() noexcept;
const char* PanelInputStatus() noexcept;
}
