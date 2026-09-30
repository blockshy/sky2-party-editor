<#
.SYNOPSIS
为统一宿主生成第三种模块分发；不修改原 ASI/Standalone 包与安装器。
.DESCRIPTION
只打包明确白名单载荷、启用清单及许可，不包含玩家设置、资源、存档或宿主二进制。
模块清单由 Hub 识别；安装、冲突检查和备份由 Hub 安装工具执行。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BinaryPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$projectRoot = Split-Path $PSScriptRoot -Parent
$exports = @(Get-PartyBinaryExports $BinaryPath)
if ($exports.Count -ne 1 -or $exports -cnotcontains 'Sky2Module_Query') {
    throw '必须提供只导出 Sky2Module_Query 的队伍 Hub 模块，不能使用原 ASI 或 XInput DLL。'
}
$cmake = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')
if ($cmake -notmatch 'project\(Sky2PartyEditor VERSION (\d+\.\d+\.\d+)') { throw '无法读取项目版本。' }
$version = $Matches[1]
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot ('release/' + $version) }
Assert-PartyPlainPath $OutputDirectory
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$stage = Join-Path $outputRoot ('.stage-hub-' + [Guid]::NewGuid().ToString('N'))
$moduleDirectory = Join-Path $stage 'dist/plugins/Sky2ModHub/modules'
$licenseDirectory = Join-Path $stage 'dist/plugins/Sky2ModHub/licenses'
New-Item -ItemType Directory -Path $moduleDirectory, $licenseDirectory -Force | Out-Null
Copy-Item -LiteralPath $BinaryPath -Destination (Join-Path $moduleDirectory 'Sky2PartyEditor.module.dll')
$manifest = "[Module]`nId=party`nBinary=Sky2PartyEditor.module.dll`nAbi=1`nEnabled=1`n"
[IO.File]::WriteAllText((Join-Path $moduleDirectory 'Sky2PartyEditor.module.ini'), $manifest, [Text.UTF8Encoding]::new($false))
# 模块本身不链接 ImGui/MinHook；保留项目和来源说明全文，方便与原分发统一核验。
$sections = @('Sky2 Party Editor Hub Module - Licenses and notices')
foreach ($relative in @('LICENSE','THIRD_PARTY_NOTICES.md','licenses/MinHook.txt','licenses/ImGui.txt')) {
    $sections += "`n" + [IO.File]::ReadAllText((Join-Path $projectRoot $relative)).Replace("`r`n", "`n")
}
[IO.File]::WriteAllText((Join-Path $licenseDirectory 'Sky2PartyEditor-LICENSES.txt'), ($sections -join "`n"), [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/HUB_MODULE.md') -Destination (Join-Path $stage 'README.md')
$archive = Join-Path $outputRoot "Sky2PartyEditor-$version-HubModule.zip"
Assert-PartyPlainPath $archive
Compress-Archive -LiteralPath @((Join-Path $stage 'README.md'), (Join-Path $stage 'dist')) -DestinationPath $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-PartyPlainPath ($archive + '.sha256')
[IO.File]::WriteAllText(($archive + '.sha256'), "$hash  $([IO.Path]::GetFileName($archive))`n", [Text.UTF8Encoding]::new($false))
Write-Output "模块包：$archive"
Write-Output "暂存目录：$stage"
Write-Output "SHA-256：$hash"
