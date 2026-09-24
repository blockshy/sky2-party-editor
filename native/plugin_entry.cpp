// 插件只由 Ultimate ASI Loader 加载，不导出或替换任何 XInput 函数。
#include "runtime.h"

extern "C" __declspec(dllexport) void InitializeASI() noexcept {
    // UAL 可能重复询问入口；Start 内部保证每个模块只启动一次工作线程。
    sky2party::Start();
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        sky2party::ConfigureModule(module);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
