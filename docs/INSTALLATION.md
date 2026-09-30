# 安装、更新与卸载

[返回首页](../README.md) · [使用指南](USAGE.md) · [测试与排错](TESTING.md)

本指南适用于 **0.6.0**，也包含从 0.5.0 升级的步骤。下载 [0.6.0 Standalone 独立版或 ASI 插件版](https://github.com/blockshy/sky2-party-editor/releases/tag/v0.6.0)，完整解压后再操作。两种分发功能相同，不能同时安装。

当前源码另提供 [Sky2 Mod Hub](https://github.com/blockshy/sky2-mod-hub) 模块版。它使用宿主的安装、备份和回滚工具，具体路径及从原入口切换的方法见 [Hub 模块版](HUB_MODULE.md)。下文 `Install-Mod.ps1` / `Uninstall-Mod.ps1` 只管理原 Standalone / ASI，不用于安装 HubModule；同一队伍功能三种入口不能混装。

## 兼容版本

游戏目录须包含 `sora_2nd.exe`。支持 Windows x64、DirectX 11，已核验 EXE 的 SHA-256 为：

```text
d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf
```

可用 `Get-FileHash .\sora_2nd.exe -Algorithm SHA256` 核对。其他构建会被拒绝；不要修改校验值绕过检查，应等待适配。

所有文件操作前都应正常退出游戏。手动安装不需要开发工具；包内脚本需要 **PowerShell 7**。安装脚本不会下载依赖、修改存档或自动修复编成。

## 分发与 Loader

| 分发 | 插件入口 | 配置、日志与许可目录 |
| --- | --- | --- |
| 独立版 | `xinput1_4.dll` | `Sky2PartyEditor/` |
| ASI 版 | `plugins/Sky2PartyEditor.asi` | `plugins/Sky2PartyEditor/` |

独立版自带本 Mod 的 XInput 转发入口，不是多 Mod Loader。若根目录已有其他 Mod 或公共 Loader 的 `xinput1_4.dll`，不要覆盖；改选 ASI 版。

ASI 版需要公共 **Ultimate ASI Loader 9.7.4 x64**，可选择以下来源：

- [宝箱 Mod 0.6.0 的公共 Loader 包](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0)：下载 `Sky2ModLoader-UAL-9.7.4.zip`，按包内说明安装。
- [Ultimate ASI Loader 官方 9.7.4 发布页](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)：使用 **x64** 包，将其中的 Loader DLL 按官方说明命名为 `xinput1_4.dll` 放入游戏根目录。不要用 Win32 包。

已配置上述公共 Loader 时无需重复安装。本项目的 ASI 安装、更新和卸载均不处理 Loader。若同时使用宝箱 Mod，请将宝箱与队伍两者都安装为 ASI 版。

## 手动安装

把对应发行包 **`dist` 里的内容**按原目录结构复制到游戏根目录。不要把 README、docs 或 tools 复制进游戏，也不要多套一层 dist 文件夹。

独立版：

```text
游戏目录/
├─ sora_2nd.exe
├─ xinput1_4.dll
└─ Sky2PartyEditor/
   └─ LICENSES.txt
```

ASI 版（根目录 Loader 需另行安装）：

```text
游戏目录/
├─ sora_2nd.exe
├─ xinput1_4.dll                 ← 公共 Ultimate ASI Loader
└─ plugins/
   ├─ Sky2PartyEditor.asi
   └─ Sky2PartyEditor/
      └─ LICENSES.txt
```

若目标已有同名文件，先确认来源；来源不明时不要覆盖。游戏启动后会在对应数据目录生成 `party.log`，应用面板设置后保存 `settings.ini`。

## 脚本安装与更新

在解压后的发行包目录运行，下例路径仅为占位，请替换为自己的游戏目录：

```powershell
pwsh -NoProfile -File .\tools\Install-Mod.ps1 -GamePath 'C:\Games\Trails in the Sky 2nd Chapter'
```

同一分发的安装与更新使用同一个命令；脚本按包内清单选择独立版或 ASI 版。加 `-WhatIf` 可预检并查看计划而不写入。

更新前退出游戏。脚本仅替换已识别的本项目二进制与许可，保留配置和日志。手动更新也只需替换对应分发的这两个文件，不必删除整个数据目录。

请使用新包的脚本更新或卸载旧版。旧包未必识别新版本文件，遇到未知指纹会中止。保留完整发行包，勿混搭不同版本的脚本和清单。

### 从 0.5.0 升级

退出游戏，下载与原安装方式相同的 0.6.0 包，用新包脚本执行上面的安装命令；或手动替换该分发的入口文件和 `LICENSES.txt`。保留自己的 `settings.ini` 和 `party.log`，无需先卸载或重置设置。八语界面随游戏文字语言自动生效，不需要新增 INI 配置。

ASI 用户无需替换公共 Loader。若同时更换独立版/ASI 版，请按下一节迁移，不要覆盖根目录中来源不同的 `xinput1_4.dll`。

## 切换分发

1. 退出游戏，用**原分发包**的卸载脚本，或手动移除已确认属于本项目的二进制及 `LICENSES.txt`。
2. 独立版转 ASI：确认原本的队伍 `xinput1_4.dll` 已移除，再按上文安装公共 Loader 和队伍 ASI。若旧 DLL 来自其他项目，按该项目自己的迁移说明处理。
3. ASI 转独立版：先确认没有其他插件依赖公共 Loader。Loader 的移除按其自身说明操作，队伍脚本不会替你删除它；根目录仍有 Loader 时不能安装独立版。
4. 安装目标分发。两个数据目录互相独立，脚本不自动迁移配置或日志；如需沿用设置，退出游戏后手动复制自己的 `settings.ini` 到目标数据目录。

不要同时保留两种队伍入口。运行时重复实例保护只是防止重复挂钩，不是推荐的安装方式。

## 卸载前恢复固定队员

固定队员换到后备后仍保留原生固定标记，恢复原版限制时可能被编成页面隐藏。角色通常仍在队伍记录里，头像消失并不等于培养数据被删除。

1. 保持 Mod 安装并开启“解除固定队员”，读取要继续使用的存档。
2. 在原生编成中把面板提示的固定队员换回主力，返回普通探索。
3. 关闭“解除固定队员”。面板会检查四支队伍，仍有待恢复角色或无法确认数据时会拒绝关闭。提示其他队伍时，需要在游戏允许切换该队伍的阶段处理，或恢复修改前的存档。
4. 关闭成功后另存、退出，再卸载。多个使用过 Mod 的存档需分别处理。

如果已卸载后发现角色不见，可先重新安装并开启“解除固定队员”，读档后按上述步骤恢复。仍无法找到时请核对原始备份，勿因头像未显示而重复新增。

此检查针对固定后备的原版可见性，不负责恢复完整原队伍或回退剧情。直接删除插件、手动修改 INI、将 `Enabled` 设为 0，均不能替代以上恢复步骤。

## 卸载文件

完成固定队员恢复并退出游戏后，可在所安装分发的完整发行包内执行：

```powershell
pwsh -NoProfile -File .\tools\Uninstall-Mod.ps1 -GamePath 'C:\Games\Trails in the Sky 2nd Chapter'
```

脚本只删除已识别的本项目入口及许可，保留配置、日志和其他 Mod。也可手动删除：

| 分发 | 仅删除确认属于本项目的文件 |
| --- | --- |
| 独立版 | `xinput1_4.dll`、`Sky2PartyEditor/LICENSES.txt` |
| ASI 版 | `plugins/Sky2PartyEditor.asi`、`plugins/Sky2PartyEditor/LICENSES.txt` |

数据目录中自己的配置和日志可保留，确认不再需要后再自行清理。不要递归删除 `plugins`，不要把公共 Loader 当作队伍 Mod 文件删除。卸载不会撤销已保存的角色加入、武器或战技补足。

## 手动与脚本混用及冲突保护

脚本依赖**包内可信清单与文件哈希**识别当前及已知历史文件，不信任游戏目录中的安装收据。因此原样手动安装后可用对应包脚本更新/卸载，脚本安装后也可手动卸载。

未知或被修改的同名 DLL、ASI、许可文件会在预检阶段中止计划，不按文件名强行认领。独立版不会覆盖未知 DLL 或公共 Loader；ASI 版不会替换根目录入口。符号链接、目录联接等重解析点也会被拒绝。

脚本会检查运行中的游戏，并用目录互斥锁协调本安装器的并发操作；仍应避免同时用其他工具改动目标目录。遇到拒绝时先确认文件归属，不要修改清单或绕过哈希检查。
