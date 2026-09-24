// 独立控制面板入口。只在宿主哈希验证通过后安装；界面线程不操作原生角色或存档。
#pragma once
#include <Windows.h>
#include <cstdint>
namespace sky2party {
bool InstallPanel(HMODULE module, uintptr_t executableBase) noexcept;
}
