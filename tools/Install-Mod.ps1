<#
.SYNOPSIS
安装或更新当前分发；未知同名文件中止整个计划，不覆盖公共 Loader 或其他 Mod。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath,
    [string]$PackageRoot=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$entries = @(Get-PartyPackage $PackageRoot)
$root = Get-PartyGameRoot $GamePath
$mutex = Enter-PartyInstallMutex $root
try {
    # 必须先全部预检，不能先装 DLL 再发现许可文件冲突。
    Assert-PartyOtherDistributionAbsent $root $entries
    foreach ($entry in $entries) { $null = Get-PartyExistingHash (Join-Path $root $entry.Path) $entry }
    if ($PSCmdlet.ShouldProcess($root, "安装或更新 Sky2PartyEditor $($entries[0].Distribution) 及其许可")) {
        foreach ($entry in $entries) { Write-PartyFile (Join-Path $root $entry.Path) $entry }
        if ($entries[0].Distribution -eq 'ASI') {
            Write-Output '队伍 ASI 已安装。请确认已配置兼容的 Ultimate ASI Loader；本脚本未修改根DLL。'
        } else { Write-Output '队伍独立版已安装。请勿同时安装队伍ASI入口。' }
    }
} finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
