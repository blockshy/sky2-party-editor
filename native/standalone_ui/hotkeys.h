// 独立 Mod 的快捷键合同。三个仓库静态编入同版实现；只有冲突登记表使用
// 进程内共享 POD 映射，绝不跨 DLL 保存 C++ 对象、回调或字符串指针。
#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sky2solo {
constexpr size_t MaxHotkeys = 16;
constexpr uint8_t HotkeyCtrl = 1, HotkeyAlt = 2, HotkeyShift = 4;
struct HotkeyBinding {
    uint16_t key = 0;
    uint8_t modifiers = 0;
    // 这里只保存 View 之外的一个数字按钮。0 表示未设置手柄绑定；View 是
    // 固定前缀，不能把单按 A/B、方向或摇杆轴改成全局快捷键。
    uint16_t pad = 0;
    bool operator==(const HotkeyBinding& other) const noexcept {
        return key == other.key && modifiers == other.modifiers && pad == other.pad;
    }
    bool operator!=(const HotkeyBinding& other) const noexcept { return !(*this == other); }
};
struct HotkeyDefinition {
    const char* id;
    const char* label;
    HotkeyBinding defaults;
    bool opensWindow = false;
};
struct HotkeySnapshot {
    uint64_t revision = 0;
    size_t count = 0;
    std::array<HotkeyBinding, MaxHotkeys> bindings{};
};

// 每个独立 DLL 只初始化自己的动作。dataDirectory 已由项目运行时核验，
// 本组件仍独立验证 shortcuts.ini 的路径/文件类型；失败回退保留可开的窗口。
// 注册及配置 I/O 仅发生于初始化或用户保存，不得在游戏输入热路径执行。
bool InitializeHotkeys(uint32_t owner, const char* moduleLabel, const wchar_t* dataDirectory,
    const HotkeyDefinition* definitions, size_t count) noexcept;
HotkeySnapshot ReadHotkeys() noexcept;
size_t HotkeyCount() noexcept;
const HotkeyDefinition* HotkeyInfo(size_t index) noexcept;
std::string HotkeyNotice();
// 仅检查当前候选，返回空字符串表示可用。冲突返回占用的 Mod 和动作名称。
// Commit 再次原子校验，磁盘保存成功后才发布新版本；失败不改变实际绑定。
std::string ValidateHotkey(size_t index, const HotkeyBinding& candidate);
bool CommitHotkey(size_t index, const HotkeyBinding& candidate, std::string& error);
bool RestoreDefaultHotkeys(std::string& error);

bool ValidHotkeyKey(uint16_t key) noexcept;
bool ValidHotkeyPad(uint16_t button) noexcept;
std::string HotkeyKeyName(uint16_t key);
std::string HotkeyKeyboardText(const HotkeyBinding& binding);
std::string HotkeyPadText(const HotkeyBinding& binding);

struct HotkeyKeyboardState {
    std::array<uint8_t, 256> down{};
    uint8_t modifiers = 0;
    bool windows = false;
};
// 从本 DLL 的原生 user32 导入读取实际状态，不经过游戏的 IAT 过滤链。
HotkeyKeyboardState ReadHotkeyKeyboardState() noexcept;
bool AnyHotkeyKeyboardDown(const HotkeyKeyboardState& state) noexcept;
bool HotkeyKeyHeld(const HotkeyBinding& binding, const HotkeyKeyboardState& state) noexcept;
bool HotkeyPadHeld(const HotkeyBinding& binding, WORD buttons) noexcept;
uint32_t HotkeyPadHeldMask(const HotkeySnapshot& snapshot, WORD buttons) noexcept;
uint32_t HotkeyPadPressedMask(const HotkeySnapshot& snapshot, WORD buttons, WORD previous) noexcept;

// 每个键盘轮询源各有一个状态机。改绑、失焦后必须真正释放所有物理键，
// 再接受新按下沿；先按主键再松修饰键不能补触发。返回位序对应注册动作。
class HotkeyKeyboardTracker {
    std::array<uint8_t, 256> previous_{};
    uint64_t revision_ = ~uint64_t(0);
    bool armed_ = false;
public:
    void Reset() noexcept { *this = HotkeyKeyboardTracker{}; }
    uint32_t Update(const HotkeySnapshot& snapshot, const HotkeyKeyboardState& state, bool available) noexcept;
};

// 配置 UI 通过显式选项编辑候选，不捕获真实组合键，因此无需临时占用
// LB/RB、Esc 或其他窗口的开关键。保存/恢复成功会产生 revision 供输入重武装。
struct HotkeyEditorState {
    size_t selected = 0;
    uint64_t revision = ~uint64_t(0);
    HotkeyBinding draft{};
    bool keyPicker = false, padPicker = false;
    std::string message;
    bool messageError = false; // 保存失败用警示色；成功回执不与错误共用视觉状态。
};
void DrawHotkeySettings(HotkeyEditorState& state, int language = 0);
void ResetHotkeyEditor(HotkeyEditorState& state) noexcept;
}
