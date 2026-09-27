# Sky2 Party Editor 0.6.0

本次更新让队伍 Mod 自动跟随游戏文字语言，支持简体中文、繁体中文、日语、英语、德语、法语、西班牙语和韩语。原有四项队伍功能、键位和配置保持兼容。

## 更新内容

- 面板、操作结果、等待状态、固定队员警示和快捷键说明均支持八语；只更改语音语言不会影响面板文字。
- 角色完整名称及原生菜单术语直接读取对应语言的游戏资源，保留原文中的称谓和军衔。资源不可用时显示角色编号及提示，不使用猜测译名。
- 切换语言会同步更新已显示的结果提示，保持当前选择，不执行队伍修改。
- 使用本机 Windows 字体并检查字形覆盖。缺字时提示安装对应补充字体；发行包不附游戏文本表或字体。

## 下载选择

| 文件 | 用途 |
| --- | --- |
| `Sky2PartyEditor-0.6.0-Standalone.zip` | 独立版，使用游戏根目录 `xinput1_4.dll` |
| `Sky2PartyEditor-0.6.0-ASI.zip` | ASI 版，由公共 Ultimate ASI Loader 加载 |
| `SHA256SUMS.txt` | 两份发行包的 SHA-256 汇总 |

两种分发功能相同，**二选一安装**。与宝箱 Mod 同用时，两者都选择 ASI 版。ASI 包不捆绑 Loader，可使用[宝箱 0.6.0 提供的公共 Loader 包](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0)，或[官方 Ultimate ASI Loader 9.7.4 x64](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)。不要覆盖其他 Mod 或 Loader 的同名 DLL。

## 从 0.5.0 升级

1. 正常退出游戏，完整解压与原安装方式相同的 0.6.0 包。
2. 使用新包的 `Install-Mod.ps1` 更新；也可手动只替换本 Mod 的入口文件和 `LICENSES.txt`。
3. 保留 `settings.ini`、日志及公共 Loader。重新启动后，面板自动采用游戏文字语言，无需新增配置。

跨独立版/ASI 版迁移、未知文件拒绝处理和手动操作步骤见[安装指南](https://github.com/blockshy/sky2-party-editor/blob/v0.6.0/docs/INSTALLATION.md)。键位及角色补足规则见[使用指南](https://github.com/blockshy/sky2-party-editor/blob/v0.6.0/docs/USAGE.md)。

## 验证与使用边界

0.6.0 已通过使用者实机验证。自动化检查覆盖多语言文本和原生名称读取、输入与设置逻辑、角色操作，以及安装、更新和卸载保护；不代表已覆盖所有剧情、设备和其他 Mod 组合。具体回归步骤见[测试与排错](https://github.com/blockshy/sky2-party-editor/blob/v0.6.0/docs/TESTING.md)。

主力仍为四人，不创建尚未初始化的角色，不重放入队剧情。请保留修改前的存档；卸载前先将面板提示的固定队员换回主力并保存，卸载不会撤销已保存的角色或培养变化。

采用 PolyForm Noncommercial 1.0.0，第三方许可随发行包保留。
