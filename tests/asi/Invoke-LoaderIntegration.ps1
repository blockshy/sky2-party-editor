<#
.SYNOPSIS
使用官方 UAL 9.7.4 x64 在新建隔离目录验证队伍／宝箱 ASI 装载。
.DESCRIPTION
所有写入限制在本项目 research 或 build 前缀目录。只复制显式给出的诊断宿主、
Loader 和插件，不启动游戏，不附加进程，不读取存档。结果仅证明装载、重复入口
保护及不支持宿主的拒绝行为；不能替代游戏内编成和双 Mod 功能测试。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$LoaderPath,
    [Parameter(Mandatory = $true)][string]$BinaryDirectory,
    [Parameter(Mandatory = $true)][string]$PartyPluginPath,
    [Parameter(Mandatory = $true)][string]$ChestPluginPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$loader = (Resolve-Path -LiteralPath $LoaderPath).Path
$binaries = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$party = (Resolve-Path -LiteralPath $PartyPluginPath).Path
$chest = (Resolve-Path -LiteralPath $ChestPluginPath).Path
$hostPath = Join-Path $binaries 'sky2_party_loader_host.exe'
if (-not (Test-Path -LiteralPath $hostPath -PathType Leaf)) { throw '缺少 sky2_party_loader_host.exe。' }

# 官方 NoPDB_x64 发布 DLL；改名为 xinput1_4.dll 不改变字节。不同版本必须重新审核，
# 不接受仅因文件名相同就把某个第三方 DLL 当作本次已验证的 UAL。
$expectedLoaderHash = '031A3E5576D91DCE1E438D36B9A3D462C7334AB4791990A8FF1E3DDC0E132DAF'
if ((Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash -ne $expectedLoaderHash) {
    throw 'Loader 不符合已审核的官方 Ultimate ASI Loader 9.7.4 x64 SHA-256。'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('research/asi-validation-' + [Guid]::NewGuid().ToString('N'))
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$prefix = $projectRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $outputRoot.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw '隔离输出必须位于本项目内部，不能选择游戏目录。'
}
$relative = $outputRoot.Substring($prefix.Length)
if ($relative -notmatch '^(research|build[^\\/]*)[\\/].+') {
    throw '隔离输出仅允许位于本项目 research 或 build 前缀子目录中。'
}
if (Test-Path -LiteralPath $outputRoot) { throw '输出目录已存在；请指定全新目录，避免旧日志误判。' }
# 检查已有祖先，禁止通过目录链接把看似项目内的输出重定向到游戏或其他目录。
$ancestor = Split-Path $outputRoot -Parent
while ($ancestor) {
    if (Test-Path -LiteralPath $ancestor) {
        $item = Get-Item -LiteralPath $ancestor -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "隔离路径经过非普通目录：$ancestor"
        }
    }
    $ancestor = Split-Path $ancestor -Parent
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null

function Invoke-Scenario {
    param([string]$Name, [bool]$WithParty, [bool]$WithChest, [bool]$WithDuplicate)
    $folder = Join-Path $outputRoot $Name
    $plugins = Join-Path $folder 'plugins'
    New-Item -ItemType Directory -Path $plugins -Force | Out-Null
    Copy-Item -LiteralPath $loader -Destination (Join-Path $folder 'xinput1_4.dll')
    Copy-Item -LiteralPath $hostPath -Destination $folder
    $arguments = @()
    if ($WithParty) {
        Copy-Item -LiteralPath $party -Destination (Join-Path $plugins 'Sky2PartyEditor.asi')
        $arguments += '--party'
    }
    if ($WithChest) {
        Copy-Item -LiteralPath $chest -Destination (Join-Path $plugins 'Sky2ChestTracker.asi')
        $arguments += '--chest'
    }
    if ($WithDuplicate) {
        Copy-Item -LiteralPath $party -Destination (Join-Path $plugins 'Sky2PartyEditorDuplicate.asi')
        $arguments += '--duplicate-party'
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $folder 'sky2_party_loader_host.exe'
    $start.WorkingDirectory = $folder
    $start.Arguments = $arguments -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    # 输出量有界；只终止本函数自己启动的诊断进程，绝不按进程名称批量停止。
    if (-not $process.WaitForExit(20000)) { $process.Kill(); throw "$Name 隔离宿主超时。" }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    [IO.File]::WriteAllText((Join-Path $folder 'host.stdout.log'), $stdout)
    [IO.File]::WriteAllText((Join-Path $folder 'host.stderr.log'), $stderr)
    if ($process.ExitCode -ne 0) { throw "$Name 失败：`n$stdout`n$stderr" }
    $partyLogPath = Join-Path $plugins 'Sky2PartyEditor/party.log'
    $chestLogPath = Join-Path $plugins 'Sky2ChestTracker/tracker.log'
    if ($WithParty) {
        $log = Get-Content -LiteralPath $partyLogPath -Raw
        if ([regex]::Matches($log, [regex]::Escape('Runtime initialization started.')).Count -ne 1 -or
            [regex]::Matches($log, [regex]::Escape('Unsupported executable; no game hooks installed.')).Count -ne 1 -or
            $log -match 'assistance active|hook installation failed') {
            throw "$Name 队伍插件的单次初始化／EXE 拒绝日志不符合预期。"
        }
    } elseif (Test-Path -LiteralPath $partyLogPath) { throw "$Name 未装队伍插件却产生队伍日志。" }
    if ($WithChest) {
        $log = Get-Content -LiteralPath $chestLogPath -Raw
        if ([regex]::Matches($log, [regex]::Escape('Unsupported executable; all hooks skipped.')).Count -ne 1 -or
            $log -match 'hook installed| active:|Overlay initialization failed') {
            throw "$Name 宝箱插件未正确拒绝假宿主。"
        }
    } elseif (Test-Path -LiteralPath $chestLogPath) { throw "$Name 未装宝箱插件却产生宝箱日志。" }
    Write-Host "PASS $Name"
    [PSCustomObject]@{ Scenario = $Name; ExitCode = $process.ExitCode; Folder = $folder }
}

$results = @(
    Invoke-Scenario 'party-only' $true $false $false
    Invoke-Scenario 'chest-only' $false $true $false
    Invoke-Scenario 'party-and-chest' $true $true $false
    Invoke-Scenario 'duplicate-party-and-chest' $true $true $true
)
$report = [ordered]@{
    LoaderVersion = '9.7.4'
    LoaderSource = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4'
    LoaderSha256 = (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash
    PartySha256 = (Get-FileHash -LiteralPath $party -Algorithm SHA256).Hash
    ChestSha256 = (Get-FileHash -LiteralPath $chest -Algorithm SHA256).Hash
    HostSha256 = (Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash
    Scope = 'Actual UAL load, InitializeASI idempotence, duplicate party guard, ordinal forwarding, and unsupported-host rejection only; no live-game compatibility claim.'
    Results = $results
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'report.json') -Encoding UTF8
Write-Output "隔离验证通过，证据：$outputRoot"
