// 独立版入口：公开 XInput 1.4 API 原样转发给系统，仅额外启动同一份队伍功能。
// 这不是通用 ASI Loader，不遍历或加载其他 Mod；组合安装请使用 ASI 分发。
#include "runtime.h"
#include <Xinput.h>
#include <mutex>
#include <string>

namespace {
HMODULE SystemXInput() noexcept {
    static HMODULE systemModule = nullptr;
    static std::once_flag once;
    try {
        std::call_once(once, [] {
            // 同名代理已加载时，只有 basename 即使配 SEARCH_SYSTEM32 也可能命中
            // 本模块。必须明确传入系统目录绝对路径，防止递归转发或加载游戏目录文件。
            wchar_t directory[MAX_PATH]{};
            const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
            if (!length || length >= MAX_PATH) return;
            const auto path = std::wstring(directory, length) + L"\\xinput1_4.dll";
            systemModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        });
    } catch (...) { /* 输入 API 边界不允许异常传播给游戏。 */ }
    return systemModule;
}
template<class Function> Function SystemFunction(const char* name) noexcept {
    const auto module = SystemXInput();
    return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
}
}

extern "C" DWORD WINAPI PartyGetState(DWORD index, XINPUT_STATE* state) noexcept {
    sky2party::Start();
    using Function = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    static const auto function = SystemFunction<Function>("XInputGetState");
    return function ? function(index, state) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI PartySetState(DWORD index, XINPUT_VIBRATION* vibration) noexcept {
    sky2party::Start();
    using Function = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    static const auto function = SystemFunction<Function>("XInputSetState");
    return function ? function(index, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI PartyGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES* capabilities) noexcept {
    using Function = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    static const auto function = SystemFunction<Function>("XInputGetCapabilities");
    return function ? function(index, flags, capabilities) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI PartyEnable(BOOL enabled) noexcept {
    using Function = void(WINAPI*)(BOOL);
    static const auto function = SystemFunction<Function>("XInputEnable");
    if (function) function(enabled);
}
extern "C" DWORD WINAPI PartyGetBatteryInformation(DWORD index, BYTE type, XINPUT_BATTERY_INFORMATION* battery) noexcept {
    using Function = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
    static const auto function = SystemFunction<Function>("XInputGetBatteryInformation");
    return function ? function(index, type, battery) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI PartyGetKeystroke(DWORD index, DWORD reserved, XINPUT_KEYSTROKE* keystroke) noexcept {
    using Function = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_KEYSTROKE*);
    static const auto function = SystemFunction<Function>("XInputGetKeystroke");
    return function ? function(index, reserved, keystroke) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI PartyGetAudioDeviceIds(DWORD index, LPWSTR renderId, UINT* renderCount,
    LPWSTR captureId, UINT* captureCount) noexcept {
    using Function = DWORD(WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
    static const auto function = SystemFunction<Function>("XInputGetAudioDeviceIds");
    return function ? function(index, renderId, renderCount, captureId, captureCount) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        sky2party::ConfigureModule(module);
        DisableThreadLibraryCalls(module);
        // Steam Input 可能接管全部 XInput 调用，因此不能等首次 GetState 才启动。
        // 此处仅派发线程、不等待、不安装挂钩；线程在加载锁释放后执行初始化。
        // Start 的模块内 once_flag 与运行时进程级命名互斥体共同阻止双入口重复生效。
        sky2party::Start();
    }
    return TRUE;
}
