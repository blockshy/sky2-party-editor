// 两份 DLL 各自静态链接真实引擎，通过纯 C 值参数证明共享登记和事务互斥。
// 测试 DLL 不安装挂钩，不读玩家文件；调用者提供独立测试目录。
#include "hotkeys.h"
#include <cstdio>
extern "C" __declspec(dllexport) bool HkInit(unsigned owner, const wchar_t* path, unsigned variant) {
    const sky2solo::HotkeyDefinition entries[]{
        {"window.open", "Window", {static_cast<uint16_t>(VK_F7 + variant),0,
            static_cast<uint16_t>(variant ? XINPUT_GAMEPAD_DPAD_LEFT : XINPUT_GAMEPAD_DPAD_UP)},true},
        {"feature.toggle", "Feature", {static_cast<uint16_t>(VK_F1 + variant),sky2solo::HotkeyCtrl,0},false}};
    return sky2solo::InitializeHotkeys(owner, variant ? "Fixture B" : "Fixture A", path, entries, 2);
}
extern "C" __declspec(dllexport) bool HkCommit(unsigned index, unsigned key, unsigned mods, unsigned pad) {
    std::string error;
    return sky2solo::CommitHotkey(index,{static_cast<uint16_t>(key),static_cast<uint8_t>(mods),static_cast<uint16_t>(pad)},error);
}
extern "C" __declspec(dllexport) bool HkReset() { std::string error; return sky2solo::RestoreDefaultHotkeys(error); }
extern "C" __declspec(dllexport) sky2solo::HotkeySnapshot HkRead() { return sky2solo::ReadHotkeys(); }
extern "C" __declspec(dllexport) bool HkValidate(unsigned index, unsigned key, unsigned mods, unsigned pad, char* error, unsigned size) {
    const auto message=sky2solo::ValidateHotkey(index,{static_cast<uint16_t>(key),static_cast<uint8_t>(mods),static_cast<uint16_t>(pad)});
    if(error&&size)snprintf(error,size,"%s",message.c_str());return message.empty();
}
