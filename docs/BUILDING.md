# 构建与开发

[返回首页](../README.md) · [参与贡献](../CONTRIBUTING.md) · [游戏内验证](TESTING.md)

当前源码保留 **0.6.0** 业务版本，并新增 Hub 模块目标。默认构建输出独立版 `xinput1_4.dll` 和插件版 `Sky2PartyEditor.asi`；存在下述 SDK 时可额外输出模块。构建、测试和打包不会安装插件或修改游戏存档。重现原双分发发行请检出 [`v0.6.0`](https://github.com/blockshy/sky2-party-editor/tree/v0.6.0) 标签；Hub 接入位于当前源码，不在该历史标签中。

## 环境与依赖

完整插件构建需要 Windows x64、Visual Studio C++ 工具链及 Windows SDK、CMake 3.20+、Git、PowerShell 7。以下示例使用 Ninja，并在 **x64 Native Tools / MSVC 开发者终端**执行。安装隔离测试另需 Python 3.9+。

| 依赖 | 用途 | 管理方式 |
| --- | --- | --- |
| MinHook | 原生函数与输入/绘制挂钩 | 固定提交与逐文件 SHA-256 |
| Dear ImGui | 独立控制面板，Win32 + DX11 后端 | 固定提交与逐文件 SHA-256 |
| Ultimate ASI Loader 9.7.4 x64 | ASI 的外部加载器、可选集成测试 | 另行取得，不由依赖脚本下载或分发 |

源码版本及指纹由 [`dependencies.json`](../dependencies.json) 固定。原两版静态链接 MinHook 和 ImGui；模块版改用 Hub 公共服务。八语字体读取本机 Windows 字库，按实际字形覆盖检查，不附带字体文件。完整第三方许可见 [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)。

构建不需要游戏资源。运行时才从已验证游戏目录只读加载八种语言的角色名和菜单用词；不把原始表或生成名称目录写入发行包。实现和翻译维护见[多语言界面](LOCALIZATION.md)。

## 准备与编译

```powershell
pwsh -NoProfile -File .\tools\Setup-Dependencies.ps1
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

准备脚本只导出锁定的必要源码到 `.deps`，已有快照只验证，不覆盖被修改的文件。离线时可使用处于对应固定提交、工作区干净的 Git 仓库：

```powershell
pwsh -NoProfile -File .\tools\Setup-Dependencies.ps1 `
  -LocalSource 'D:\Sources\minhook' -ImguiLocalSource 'D:\Sources\imgui'
```

示例为占位路径。自定义依赖输出可用脚本 `-Destination`，并向 CMake 传入 `-DPARTY_DEPS_DIR=该目录`。

默认 `PARTY_BUILD_MOD=ON` 会生成两个入口，无需分别编译。Visual Studio 等多配置生成器需要在构建和 CTest 命令中分别加 `--config Release`、`-C Release`，产物通常位于构建目录的 `Release/` 子目录。

## 可选 Hub 模块目标

相邻目录含 `sky2-mod-hub/sdk/sky2_hub.h` 时，Windows x64/MSVC 默认额外生成 `Sky2PartyEditor.module.dll`。原 `Sky2PartyEditor.asi` 和 `xinput1_4.dll` 始终保留。可以用 `-DPARTY_BUILD_HUB_MODULE=OFF` 完全关闭新目标，旧双分发不要求 SDK；也可用 `-DPARTY_HUB_SDK_DIR=绝对目录` 指定 SDK。

从 [Hub 仓库](https://github.com/blockshy/sky2-mod-hub)取得 **0.5.0 或更新的 SDK**。下例在队伍仓库根目录执行，SDK 放在相邻仓库，不复制宿主源码或依赖缓存进本仓库：

```powershell
git clone --branch v0.5.0 https://github.com/blockshy/sky2-mod-hub.git ../sky2-mod-hub
$hubSdk = (Resolve-Path '../sky2-mod-hub/sdk').Path
cmake -S . -B build-hub -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON `
  -DPARTY_BUILD_HUB_MODULE=ON "-DPARTY_HUB_SDK_DIR=$hubSdk"
cmake --build build-hub
ctest --test-dir build-hub --output-on-failure
```

模块业务源独立编译，通过 SDK 的 MinHook 适配头转交宿主，既不静态链接 ImGui，也不链接私有 MinHook 或 DX11 后端。`party_hub_panel_tests` 驱动真实页面与假 UI 验证确认生命周期；`party_hub_module_tests` 装载真实模块校验 ABI 和错误宿主拒绝。模块打包及目录见 [Hub 模块版](HUB_MODULE.md)。

## 测试

自动测试覆盖事务回退、真实条件指令、角色名单与补足规则、固定后备检查、命令队列失效、输入状态机，以及安装/卸载归属。新增检查覆盖八语文案与格式参数、文字/语音语言分离，以及合成 FPAC/TBL 的解析边界和角色 ID 配对。角色服务及补足测试使用合成对象和受控替身，不操作真实游戏或存档。

```powershell
# 单独运行安装/打包检查；使用临时目录中的合成游戏环境。
python -m unittest discover -s tests/packaging -v
```

CMake 找到 Windows Python 后会把该检查加入 CTest。请查看实际测试列表及结果，未注册的可选集成检查不算已经通过。

- [UAL 隔离测试](../tests/asi/README.md)：使用真正的官方 Loader 和生产插件，验证加载、XInput 转发、重复入口保护和不支持宿主的拒绝路径；需要自行提供已核验文件。
- [Present 链测试](../tests/present_chain/README.md)：两个独立静态 MinHook DLL 与真实 WARP 交换链，验证并发安装的丢链复现、串行及就绪协议；不代表全部游戏内 Mod 组合兼容。
- [游戏内回归](TESTING.md)：编成、角色战斗、存取档和实际设备输入仍需验证，不能用假宿主替代。

不构建插件时，可在其他支持 C++17 的平台执行纯逻辑测试：

```sh
cmake -S . -B build-logic -DPARTY_BUILD_MOD=OFF -DPARTY_BUILD_HUB_MODULE=OFF -DBUILD_TESTING=ON
cmake --build build-logic
ctest --test-dir build-logic --output-on-failure
```

Windows 专项与完整运行时不会出现在非 Windows 测试列表中。

## 打包

打包脚本读取 CMake 项目版本，只接收 x64 PE DLL，按所选分发的白名单生成独立包。原两版仍要求 `-CompanionBinaryPath` 提供同版本的另一入口，用于识别本产品分发冲突；不把另一入口装入本包。

```powershell
pwsh -NoProfile -File .\tools\Package-Mod.ps1 `
  -BinaryPath .\build-release\xinput1_4.dll -Distribution Standalone `
  -CompanionBinaryPath .\build-release\Sky2PartyEditor.asi

pwsh -NoProfile -File .\tools\Package-Mod.ps1 `
  -BinaryPath .\build-release\Sky2PartyEditor.asi -Distribution ASI `
  -CompanionBinaryPath .\build-release\xinput1_4.dll

# HubModule 只接受仅导出 Sky2Module_Query 的模块 DLL；不要求旧入口伴随产物。
pwsh -NoProfile -File .\tools\Package-Mod.ps1 `
  -BinaryPath .\build-hub\Sky2PartyEditor.module.dll -Distribution HubModule
```

默认输出目录为 `release/0.6.0/`，也可用 `-OutputDirectory` 指定。原两版包括运行载荷、安装工具及玩家指南；HubModule 包只含模块、启用清单、合并许可与 Hub 指南，不含宿主、公共 Loader 或原安装器。许可文件合并项目与第三方完整条款。不会递归打包工作目录、依赖缓存、研究、游戏原文或生成数据。

每个新生成的 ZIP 都有对应 `.sha256` 文件；原 0.6.0 Release 仍为已有两份 ZIP，本次源码更新不新增队伍 Release。发布前，应对最终 ZIP 核对哈希、实际解压路径、许可、安装/更新/卸载与分发冲突；不要将不同构建的二进制和清单拼接使用。

## 实现边界

入口负责加载及 XInput 转发，功能逻辑共用；控制面板只读取值快照、提交请求，游戏线程在安全状态复核并执行。原两版绘制使用独立 ImGui 上下文并还原渲染状态，输入兼容需同时考虑原始 XInput 与其他插件的过滤层；HubModule 的输入、图形和页面布局统一属于宿主。

修改原生规则前必须确认目标 EXE 和指令签名。角色补足不得清除已有培养或重放剧情；发生部分原生变化后必须明确报告，不能宣称完整回滚。安装器以包内清单和可信历史哈希识别文件，任何新路径或分发都需保持未知文件、公共 Loader 和玩家配置的保护。
