<#
.SYNOPSIS
只卸载当前分发哈希白名单识别出的两个文件，保留配置、日志及其他 Mod。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath,
    [string]$PackageRoot=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$entries = @(Get-PartyPackage $PackageRoot)
# 游戏更新之后也应允许移除自己已知的插件；仍要求游戏退出且路径确属游戏目录。
$root = Get-PartyGameRoot $GamePath $false
$mutex = Enter-PartyInstallMutex $root
try {
    foreach ($entry in $entries) { $null = Get-PartyExistingHash (Join-Path $root $entry.Path) $entry }
    # 文件卸载不读取或改写游戏存档，无法替玩家恢复被换到后备的固定角色。
    # 在执行前明确给出原生恢复流程；同样适用于手动安装后使用本脚本卸载。
    Write-Output '卸载前请确认：已将固定队员换回主力，关闭“解除固定队员”并保存。未恢复时原版编成可能隐藏这些角色；本脚本不会修改存档。详见 docs/INSTALLATION.md。'
    if ($PSCmdlet.ShouldProcess($root, "卸载已知的 Sky2PartyEditor $($entries[0].Distribution) 及其许可")) {
        foreach ($entry in $entries) {
            $target = Join-Path $root $entry.Path
            Assert-PartyGameStopped
            if (Get-PartyExistingHash $target $entry) { Remove-Item -LiteralPath $target }
        }
        Write-Output '当前队伍Mod分发已卸载；其他插件、配置和日志均保留。未删除任何目录。'
    }
} finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
