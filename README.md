# Sky2 Party Editor

《空之轨迹 the 2nd》队伍编辑 Mod。通过游戏原生编成页面调整主力与后备，并提供支持键盘、鼠标和 Xbox 手柄的控制面板。

**当前版本：0.5.0** · [下载发行包](https://github.com/blockshy/sky2-party-editor/releases/tag/v0.5.0) · [更新记录](CHANGELOG.md)

## 功能

| 功能 | 说明 | 默认 |
| --- | --- | --- |
| 解除固定队员 | 允许固定队员与可用后备互换 | 开启 |
| 随处编成 | 普通探索中直接使用原生队伍键，或从主菜单进入编成 | 开启 |
| 暂不可选后备 | 允许选择已显示、但暂时不可选的后备 | 开启 |
| 未入队角色（实验） | 逐人加入支持角色，或显示隐藏后备；必要时补足缺失的基础武器和战技 | 关闭 |

四项功能可独立切换，设置在可自由行动且原生菜单关闭时应用。**主力仍为四人上限**；战斗、过场、换图及剧情编成锁仍受限制，剧情也可能重新编队。

支持九名常规队员与五名可战斗客串。仅处理存档中已有核心培养数据的角色，不创建尚未初始化的角色，不重放入队剧情。完整范围及补足规则见[使用指南](docs/USAGE.md#角色与数据补足)。

## 选择分发

两种分发功能相同，**二选一安装**。

| 发行包 | 适用情况 | 入口 |
| --- | --- | --- |
| `Sky2PartyEditor-0.5.0-Standalone.zip` | 只使用本 Mod，且游戏根目录没有其他同名 DLL | 根目录 `xinput1_4.dll` |
| `Sky2PartyEditor-0.5.0-ASI.zip` | 与宝箱 Mod 等 ASI 插件共用公共 Loader | `plugins/Sky2PartyEditor.asi` |

ASI 版需要另行安装 **Ultimate ASI Loader 9.7.4 x64**；本项目不捆绑或管理公共 Loader。与 [Sky2 Chest Tracker](https://github.com/blockshy/sky2-chest-tracker) 同用时，两者都选择 ASI 版。Loader 下载和双版迁移见[安装指南](docs/INSTALLATION.md)。GitHub 自动生成的 Source code 压缩包不能直接安装。

## 快速使用

1. 按[安装指南](docs/INSTALLATION.md)手动复制文件，或使用包内脚本安装。
2. 进入普通探索，按 **F11** 或 **View + LS（按下左摇杆）** 打开控制面板。
3. 用方向键 / 十字键选择，**Enter / A** 确认，**Esc / B** 关闭；鼠标也可点击。
4. 关闭面板后，用游戏原生队伍键打开编成并调整主力。Xbox 默认键为 **X**，改键后以游戏提示为准。

角色加入需要连续确认两次。切换项目、关闭面板、超时或场景变化会取消确认。键盘与手柄提示支持热切换。

**保留一份修改前的存档。** 正常保存会保留队伍调整及数据补足，关闭功能或卸载不会撤销这些变化。卸载前，需在原生编成中把面板提示的固定队员换回主力并保存；具体步骤见[卸载前恢复](docs/INSTALLATION.md#卸载前恢复固定队员)。

## 文档

| 文档 | 内容 |
| --- | --- |
| [安装指南](docs/INSTALLATION.md) | 手动与脚本安装、更新、卸载、分发切换、文件冲突 |
| [使用指南](docs/USAGE.md) | 面板键位、角色范围、补足规则、配置 |
| [测试与排错](docs/TESTING.md) | 游戏内回归步骤、日志与反馈信息 |
| [构建指南](https://github.com/blockshy/sky2-party-editor/blob/main/docs/BUILDING.md) | 依赖、编译、自动测试与打包 |
| [参与贡献](https://github.com/blockshy/sky2-party-editor/blob/main/CONTRIBUTING.md) | 问题反馈、代码变更与公开提交边界 |

## 兼容与许可

支持 Windows x64 的已核验游戏构建，具体 EXE 指纹见[兼容版本](docs/INSTALLATION.md#兼容版本)。其他游戏更新、Mod 组合及剧情进度不能仅凭成功启动认定兼容；检测到不支持的 EXE 时不会应用游戏修改。

项目采用 **[PolyForm Noncommercial 1.0.0](https://github.com/blockshy/sky2-party-editor/blob/main/LICENSE)**。第三方来源见 [THIRD_PARTY_NOTICES.md](https://github.com/blockshy/sky2-party-editor/blob/main/THIRD_PARTY_NOTICES.md)；发行包的数据目录内附完整 `LICENSES.txt`。不分发游戏资源、存档或研究生成数据。
