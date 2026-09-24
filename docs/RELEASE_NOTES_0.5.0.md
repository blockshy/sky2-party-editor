# Sky2 Party Editor 0.5.0

首次公开发行。《空之轨迹 the 2nd》队伍编辑 Mod，使用原生编成页面调整主力与后备，并提供键盘、鼠标和 Xbox 手柄控制面板。

## 下载选择

| 文件 | 用途 |
| --- | --- |
| `Sky2PartyEditor-0.5.0-Standalone.zip` | 独立版，使用游戏根目录 `xinput1_4.dll` |
| `Sky2PartyEditor-0.5.0-ASI.zip` | ASI 版，由公共 Ultimate ASI Loader 加载 |
| `SHA256SUMS.txt` | 两份发行包的 SHA-256 汇总 |

两种分发功能相同，二选一安装。与宝箱 Mod 同用时，请将两个 Mod 都选为 ASI 版。ASI 包不包含 Loader，可使用[宝箱 0.6.0 提供的公共 Loader 包](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0)，或[官方 Ultimate ASI Loader 9.7.4 x64](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)。已有其他 `xinput1_4.dll` 时不要直接覆盖。

## 功能

- 解除固定队员、随处打开原生编成、选择暂不可选后备。
- 实验功能逐人加入或开放九名常规队员及五名可战斗客串，加入需两次确认。
- 对已有核心培养数据的角色补足空武器槽和全空战技栏，保留等级、其他装备及回路；兼容空列表保留继承 S 战技选择的情况。
- **F11 / View + LS** 开关面板，方向键 / 十字键选择，**Enter / A** 确认，**Esc / B** 关闭，支持键盘与手柄提示热切换。
- 检查固定成员后备状态，提示在关闭对应限制或卸载前换回主力。
- 提供手动和脚本安装方式，按哈希拒绝未知同名文件，更新/卸载保留配置、日志和其他 Mod。

## 使用边界

主力仍为四人；不创建尚未初始化的角色、不重放入队剧情，也不解除战斗、过场和剧情编成锁。正常保存会保留队伍及补足结果，卸载不会撤销这些变化，请保留修改前的存档。

卸载前请将面板提示的固定队员换回主力，关闭“解除固定队员”并保存。支持的游戏 EXE 指纹、完整步骤和恢复说明见[安装指南](https://github.com/blockshy/sky2-party-editor/blob/v0.5.0/docs/INSTALLATION.md)；键位及角色补足规则见[使用指南](https://github.com/blockshy/sky2-party-editor/blob/v0.5.0/docs/USAGE.md)。

采用 PolyForm Noncommercial 1.0.0。项目不分发游戏资源或存档，第三方许可随发行包保留。
