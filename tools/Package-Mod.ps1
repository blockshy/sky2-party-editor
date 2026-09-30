<#
.SYNOPSIS
按 Standalone/ASI/HubModule 分发生成精简公开发行包，只纳入明确白名单文件。
.DESCRIPTION
Standalone/ASI 必须同时提供本分发与同版本另一入口的构建产物。另一入口只用于
PE 身份校验和冲突指纹，不随当前包分发。HubModule 委托专用白名单脚本，使用
宿主安装工具；三种包都不打包研究、玩家设置或存档。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][Alias('AsiPath','DllPath')][string]$BinaryPath,
    [Parameter(Mandatory=$true)][ValidateSet('Standalone','ASI','HubModule')][string]$Distribution,
    [string]$CompanionBinaryPath,
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
# 模块采用宿主安装器管理；不扩展旧双分发安装器的文件权限或归属范围。
if ($Distribution -eq 'HubModule') {
    & (Join-Path $PSScriptRoot 'Package-HubModule.ps1') -BinaryPath $BinaryPath -OutputDirectory $OutputDirectory
    return
}
if (-not $CompanionBinaryPath) { throw 'Standalone/ASI 打包仍必须提供 CompanionBinaryPath。' }
. (Join-Path $PSScriptRoot 'Common.ps1')
$projectRoot = Split-Path $PSScriptRoot -Parent
$otherDistribution = if ($Distribution -eq 'ASI') { 'Standalone' } else { 'ASI' }
Assert-PartyBinaryKind $BinaryPath $Distribution
Assert-PartyBinaryKind $CompanionBinaryPath $otherDistribution
$source = (Resolve-Path -LiteralPath $BinaryPath).Path
$companion = (Resolve-Path -LiteralPath $CompanionBinaryPath).Path
$companionHash = (Get-FileHash -LiteralPath $companion -Algorithm SHA256).Hash.ToLowerInvariant()
$cmake = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')
if ($cmake -notmatch 'project\(Sky2PartyEditor VERSION (\d+\.\d+\.\d+)') { throw '无法读取项目版本。' }
$version = $Matches[1]
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot ('release/' + $version) }
Assert-PartyPlainPath $OutputDirectory
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$stage = Join-Path $outputRoot ('.stage-' + [Guid]::NewGuid().ToString('N'))
$paths = @(Get-PartyPaths $Distribution)
foreach ($relative in @((Split-Path ('dist/' + $paths[0]) -Parent),
    (Split-Path ('dist/' + $paths[1]) -Parent), 'tools', 'docs')) {
    New-Item -ItemType Directory -Path (Join-Path $stage $relative) -Force | Out-Null
}
Copy-Item -LiteralPath $source -Destination (Join-Path $stage ('dist/' + $paths[0]))
# 合并许可全文并统一LF，使相同内容在不同checkout换行设置下得到相同指纹。
# Required Notice、MinHook/HDE 与 ImGui 的版权条款均不能为精简包而摘除。
$sections = @('Sky2 Party Editor - Licenses and notices')
foreach ($relative in @('LICENSE','THIRD_PARTY_NOTICES.md','licenses/MinHook.txt','licenses/ImGui.txt')) {
    $sections += "`n================================================================================`n" +
        [IO.Path]::GetFileName($relative) + "`n================================================================================`n" +
        [IO.File]::ReadAllText((Join-Path $projectRoot $relative)).Replace("`r`n", "`n").Replace("`r", "`n")
}
[IO.File]::WriteAllText((Join-Path $stage ('dist/' + $paths[1])), ($sections -join "`n"), [Text.UTF8Encoding]::new($false))
foreach ($scriptName in @('Install-Mod.ps1', 'Uninstall-Mod.ps1', 'Common.ps1')) {
    # 包内脚本写UTF-8 BOM，Windows PowerShell 5.1也可正确解析中文错误消息。
    $body = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $scriptName))
    [IO.File]::WriteAllText((Join-Path $stage ('tools/' + $scriptName)), $body, [Text.UTF8Encoding]::new($true))
}
foreach ($document in @('README.md','CHANGELOG.md','LICENSE','THIRD_PARTY_NOTICES.md',
    'docs/INSTALLATION.md','docs/USAGE.md','docs/TESTING.md','docs/HUB_MODULE.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $document) -Destination (Join-Path $stage $document)
}
$known = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'Known-Files.json') | ConvertFrom-Json
function Get-KnownPartyHashes([string]$Relative) {
    $property = $known.PSObject.Properties[$Relative]
    if (-not $property) { throw "缺少固定文件的历史指纹集合：$Relative" }
    foreach ($hash in @($property.Value)) {
        if ($hash -notmatch '^[0-9a-fA-F]{64}$') { throw '历史文件指纹格式错误。' }
        $hash.ToLowerInvariant()
    }
}
$files = foreach ($relative in $paths) {
    $hashes = @(Get-KnownPartyHashes $relative)
    [ordered]@{ path=$relative;
        sha256=(Get-FileHash -LiteralPath (Join-Path $stage ('dist/' + $relative)) -Algorithm SHA256).Hash.ToLowerInvariant();
        known_sha256=$hashes }
}
$otherPath = @(Get-PartyPaths $otherDistribution)[0]
$otherHashes = @(@(Get-KnownPartyHashes $otherPath) + @($companionHash) | Select-Object -Unique)
$type = if ($Distribution -eq 'ASI') { 'asi-plugin' } else { 'standalone-proxy' }
$manifest = [ordered]@{ schema=2; product='Sky2PartyEditor'; type=$type; version=$version;
    exe_sha256=$script:PartyExeHash; files=@($files);
    conflicts=@([ordered]@{path=$otherPath; known_sha256=$otherHashes}) }
[IO.File]::WriteAllText((Join-Path $stage 'tools/manifest.json'), ($manifest | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
$null = Get-PartyPackage $stage
$archive = Join-Path $outputRoot "Sky2PartyEditor-$version-$Distribution.zip"
Assert-PartyPlainPath $archive
# 暂存目录为本次随机新建；ZIP只包含以下固定玩家文件集合，绝不递归打包仓库。
Compress-Archive -LiteralPath @((Join-Path $stage 'README.md'), (Join-Path $stage 'CHANGELOG.md'),
    (Join-Path $stage 'LICENSE'), (Join-Path $stage 'THIRD_PARTY_NOTICES.md'),
    (Join-Path $stage 'dist'), (Join-Path $stage 'tools'), (Join-Path $stage 'docs')) -DestinationPath $archive -Force
$zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-PartyPlainPath ($archive + '.sha256')
[IO.File]::WriteAllText(($archive + '.sha256'), ($zipHash + '  ' + [IO.Path]::GetFileName($archive) + "`n"), [Text.UTF8Encoding]::new($false))
Write-Output "安装包：$archive"
Write-Output "暂存目录：$stage"
Write-Output "SHA-256：$zipHash"
