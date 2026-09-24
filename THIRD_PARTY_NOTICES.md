# 来源与第三方许可

本项目采用 [PolyForm Noncommercial 1.0.0](LICENSE)。第三方组件保留各自的许可。

- ASI 启动、XInput 代理、运行目录保护与版本校验参考 [Sky2 Chest Tracker](https://github.com/blockshy/sky2-chest-tracker)，版权归 BlockShy 及贡献者所有，采用 PolyForm Noncommercial 1.0.0；本项目保留其 Required Notices。队伍功能为独立实现。
- 原生菜单挂钩静态链接 [MinHook](https://github.com/TsudaKageyu/minhook)，固定提交 `8af6b4acae5a9388fd742b56fa79ece89d96f823`。MinHook 及其反汇编器的完整许可和版权声明见源码中的 `licenses/MinHook.txt`。
- 控制面板静态链接 [Dear ImGui](https://github.com/ocornut/imgui)，固定提交 `420f1793417e39560ca39a4209c55a9f204fde13`，采用 MIT 许可。完整条款见 `licenses/ImGui.txt`；界面字体从玩家的 Windows 系统加载，不随插件分发。
- 项目许可、来源声明、MinHook 和 ImGui 完整许可合并到发行包中：独立版为 `dist/Sky2PartyEditor/LICENSES.txt`，ASI 版为 `dist/plugins/Sky2PartyEditor/LICENSES.txt`。本项目不包含游戏资源解析器。
- ASI 版由用户现有的 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) 加载。本项目不打包、安装或卸载 Loader，不修改其原有许可；独立版直接使用自身的 XInput 代理，无需 Loader。
- 原始游戏文件、解析出的资源、玩家存档、研究过程资料不公开分发。
