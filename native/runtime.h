// 独立版/ASI 公共诊断与编成挂钩接口；不依赖宝箱 Mod 或其内部对象。
#pragma once
#include <Windows.h>
#include <cstdint>

namespace sky2party {
// DllMain 登记本模块；所有文件操作、宿主校验和挂钩均由 Start 创建的工作线程执行。
void ConfigureModule(HMODULE module) noexcept;
void Start() noexcept;
#ifdef SKY2_HUB_MODULE
// 模块入口由 Hub 同步调用；复用原 ASI 数据目录并完成业务校验/挂钩，
// 不创建额外线程，不安装独立面板、输入或 Present。失败后不得卸载 DLL。
bool InitializeHubRuntime(HMODULE module, const wchar_t* dataDirectory) noexcept;
#endif
// 日志只包含本 Mod 的诊断，不记录存档内容；写入失败不影响原生游戏流程。
void Log(const char* message) noexcept;
// 只有宿主 EXE 完整 SHA-256 匹配后才能调用此入口；各代码点仍须核对原始字节。
bool InstallPartyHooks(uintptr_t executableBase) noexcept;
// 设置保存只访问插件自己的INI，不修改游戏存档。由安全帧成功应用设置后调用。
bool SaveFeaturePreferences(uint32_t featureMask) noexcept;
}
