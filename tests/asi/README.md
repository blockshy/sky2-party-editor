# UAL 隔离加载验证

这组 Windows x64 测试使用真正的 Ultimate ASI Loader 9.7.4 与生产 ASI，在没有游戏资源的假宿主中验证装载。不会启动游戏、附加游戏进程或访问存档。

## 构建和执行

在 MSVC x64 开发终端中，主工程可以使用 `add_subdirectory(tests/asi)` 构建 `sky2_party_loader_host.exe`，也可以单独构建：

```powershell
cmake -S tests/asi -B build-asi-host -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build build-asi-host

.\tests\asi\Invoke-LoaderIntegration.ps1 `
  -LoaderPath '已核验的官方 UAL 9.7.4 x64\xinput1_4.dll' `
  -BinaryDirectory '.\build-asi-host' `
  -PartyPluginPath '.\build-release\Sky2PartyEditor.asi' `
  -ChestPluginPath '已解压的正式宝箱 ASI 安装包\dist\plugins\Sky2ChestTracker.asi'
```

脚本不下载 Loader。它要求 DLL 的 SHA-256 为 `031a3e5576d91dce1e438d36b9a3d462c7334ab4791990a8ff1e3ddc0e132daf`，对应[官方 9.7.4 x64 发布](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)中的 DLL。重命名官方 DLL 不改变哈希。

默认在本项目 `research/asi-validation-随机标识/` 中创建全新目录。显式指定 `-OutputDirectory` 时，也只能使用本项目 `research/` 或 `build` 前缀目录下的全新子目录；拒绝链接祖先，不能把输出指向游戏目录。

## 验证内容

| 场景 | 检查 |
| --- | --- |
| 仅队伍插件 | UAL 完成首次初始化；拒绝假宿主；重复入口不重复初始化 |
| 仅宝箱插件 | 移除队伍插件后宝箱仍可装载并拒绝假宿主 |
| 队伍与宝箱同时安装 | 两个生产插件入口均由 UAL 调用，各自在自己的日志中拒绝假宿主 |
| 两份队伍副本与宝箱 | 两个队伍副本确实映射为不同模块，只有一份队伍运行时产生初始化及拒绝记录 |

宿主以本游戏的 ordinal 2/3 方式导入 XInput，并与独立加载的 System32 实现比较无效设备索引的返回值。首次调用 `InitializeASI` 必须由 UAL 完成；宿主在观察到初次拒绝日志后，额外调用每个入口八次并持续观察日志，队伍初始化和 EXE 拒绝记录仍必须各一条。进程结束销毁隔离环境，不模拟 ASI 热卸载。

每个场景保存 `host.stdout.log`、`host.stderr.log` 及插件自身日志；`report.json` 记录参与测试的 Loader、宿主、两个插件的哈希和结果。测试目录保留用于排查，不进入发行包。

通过测试仅证明 UAL 加载、重复入口／重复副本保护、XInput 导入转发及不支持宿主的拒绝路径。真实游戏内编成、剧情兼容、宝箱功能与队伍功能同时工作仍需实机测试。
