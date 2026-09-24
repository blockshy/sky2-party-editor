<#
.SYNOPSIS
仅管理当前分发的两个固定文件；公共 Loader、其他 Mod 与玩家运行记录不属于操作集合。
.DESCRIPTION
信任来自发行包自身及历史哈希白名单，不读取游戏目录中的安装收据。
先验证整个操作计划，再逐个检查并原子替换；不递归清理任何目录。
#>
Set-StrictMode -Version Latest
$script:PartyExeHash = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
function Get-PartyPaths([ValidateSet('Standalone','ASI')][string]$Distribution) {
    if ($Distribution -eq 'Standalone') { return @('xinput1_4.dll', 'Sky2PartyEditor/LICENSES.txt') }
    return @('plugins/Sky2PartyEditor.asi', 'plugins/Sky2PartyEditor/LICENSES.txt')
}

function Get-PartyBinaryExports([string]$Path) {
    # 仅解析打包输入的PE元数据，不装载或执行DLL。导出校验可发现ASI与XInput构建
    # 选反，但不能证明产品归属；打包输入必须来自可信本项目构建，安装归属另用哈希。
    Assert-PartyPlainPath $Path
    $binary = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path).Path)
    if ($binary.Length -lt 256 -or $binary[0] -ne 0x4D -or $binary[1] -ne 0x5A) { throw '输入不是有效的 PE 文件。' }
    $pe = [BitConverter]::ToInt32($binary, 0x3C)
    if ($pe -lt 0 -or $pe -gt $binary.Length - 24 -or
        [BitConverter]::ToUInt32($binary, $pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($binary, $pe + 4) -ne 0x8664 -or
        ([BitConverter]::ToUInt16($binary, $pe + 22) -band 0x2000) -eq 0) { throw '输入必须是 x64 PE DLL。' }
    $optional = $pe + 24
    $optionalLength = [BitConverter]::ToUInt16($binary, $pe + 20)
    $sectionCount = [BitConverter]::ToUInt16($binary, $pe + 6)
    if ($optionalLength -lt 120 -or $optional + $optionalLength -gt $binary.Length -or
        [BitConverter]::ToUInt16($binary, $optional) -ne 0x20B -or
        [BitConverter]::ToUInt32($binary, $optional + 108) -lt 1 -or $sectionCount -lt 1 -or $sectionCount -gt 96) {
        throw 'PE 可选头或导出目录无效。'
    }
    $sections = $optional + $optionalLength
    if ($sections + $sectionCount * 40 -gt $binary.Length) { throw 'PE 节表越界。' }
    # 所有RVA转换均同时限制节内范围与文件范围，损坏输入不能越界解析。
    function Get-PartyRvaOffset([uint32]$Rva, [int64]$Length) {
        for ($index = 0; $index -lt $sectionCount; ++$index) {
            $section = $sections + $index * 40
            $start = [BitConverter]::ToUInt32($binary, $section + 12)
            $size = [BitConverter]::ToUInt32($binary, $section + 16)
            $file = [BitConverter]::ToUInt32($binary, $section + 20)
            $delta = [int64]$Rva - $start
            if ($delta -ge 0 -and $delta + $Length -le $size -and
                [int64]$file + $delta + $Length -le $binary.Length) { return [int]($file + $delta) }
        }
        throw 'PE 导出RVA超出有效文件范围。'
    }
    $exportRva = [BitConverter]::ToUInt32($binary, $optional + 112)
    $directory = Get-PartyRvaOffset $exportRva 40
    $count = [BitConverter]::ToUInt32($binary, $directory + 24)
    if ($count -lt 1 -or $count -gt 65536) { throw 'PE 导出名称数量无效。' }
    $names = Get-PartyRvaOffset ([BitConverter]::ToUInt32($binary, $directory + 32)) ([int64]$count * 4)
    foreach ($index in 0..($count - 1)) {
        $rva = [BitConverter]::ToUInt32($binary, $names + $index * 4)
        $characters = [Collections.Generic.List[byte]]::new()
        for ($character = 0; $character -lt 256; ++$character) {
            $offset = Get-PartyRvaOffset ([uint32]([int64]$rva + $character)) 1
            if ($binary[$offset] -eq 0) { break }
            $characters.Add($binary[$offset])
        }
        if ($character -eq 256) { throw 'PE 导出名称未终止。' }
        [Text.Encoding]::ASCII.GetString($characters.ToArray())
    }
}

function Assert-PartyBinaryKind([string]$Path, [ValidateSet('Standalone','ASI')][string]$Distribution) {
    $exports = @(Get-PartyBinaryExports $Path)
    if ($Distribution -eq 'ASI') {
        if ($exports -cnotcontains 'InitializeASI' -or $exports -ccontains 'XInputGetState') {
            throw 'ASI分发必须使用独立InitializeASI入口，不能打包XInput代理。'
        }
    } else {
        foreach ($name in @('XInputGetState','XInputSetState','XInputGetCapabilities','XInputEnable',
            'XInputGetBatteryInformation','XInputGetKeystroke','XInputGetAudioDeviceIds')) {
            if ($exports -cnotcontains $name) { throw "独立版缺少必需的XInput导出：$name" }
        }
        if ($exports -ccontains 'InitializeASI') { throw '独立分发不能混用ASI初始化入口。' }
    }
}

function Assert-PartyPlainPath([string]$Path) {
    # 检查文件及全部父目录，阻止联接或符号链接把写入引向目标游戏目录之外。
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "拒绝操作链接或重解析点：$cursor"
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Assert-PartyGameStopped {
    if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) {
        throw '请先正常退出游戏，随后再安装、更新或卸载。'
    }
}

function Get-PartyGameRoot([string]$GamePath, [bool]$CheckVersion = $true) {
    Assert-PartyPlainPath $GamePath
    $root = (Resolve-Path -LiteralPath $GamePath -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw '游戏目录不存在。' }
    Assert-PartyGameStopped
    $exe = Join-Path $root 'sora_2nd.exe'
    Assert-PartyPlainPath $exe
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw '所选目录不包含 sora_2nd.exe。' }
    if ($CheckVersion -and (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $script:PartyExeHash) {
        throw '游戏版本不匹配，未改动文件。'
    }
    return $root
}

function Get-PartyPackage([string]$PackageRoot) {
    Assert-PartyPlainPath $PackageRoot
    $manifestPath = Join-Path $PackageRoot 'tools/manifest.json'
    Assert-PartyPlainPath $manifestPath
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.schema -ne 2 -or $manifest.product -cne 'Sky2PartyEditor' -or
        @('asi-plugin','standalone-proxy') -cnotcontains $manifest.type -or $manifest.exe_sha256 -ne $script:PartyExeHash -or
        $manifest.version -notmatch '^\d+\.\d+\.\d+$' -or @($manifest.files).Count -ne 2) {
        throw '发行包清单格式或身份不正确。'
    }
    $distribution = if ($manifest.type -ceq 'asi-plugin') { 'ASI' } else { 'Standalone' }
    $otherPath = if ($distribution -eq 'ASI') { 'xinput1_4.dll' } else { 'plugins/Sky2PartyEditor.asi' }
    if (@($manifest.conflicts).Count -ne 1 -or $manifest.conflicts[0].path -cne $otherPath -or
        @($manifest.conflicts[0].known_sha256).Count -lt 1) { throw '发行包缺少另一分发入口的冲突指纹。' }
    $otherHashes = @($manifest.conflicts[0].known_sha256)
    foreach ($hash in $otherHashes) {
        if ($hash -notmatch '^[0-9a-fA-F]{64}$') { throw '另一分发入口指纹格式错误。' }
    }
    $plan = @()
    foreach ($relative in @(Get-PartyPaths $distribution)) {
        $entries = @($manifest.files | Where-Object { $_.path -ceq $relative })
        if ($entries.Count -ne 1) { throw "发行包缺少唯一的固定文件：$relative" }
        $entry = $entries[0]
        if ($entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw '发行包 SHA-256 格式错误。' }
        $source = Join-Path $PackageRoot ('dist/' + $relative)
        Assert-PartyPlainPath $source
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "发行包文件内容不匹配：$relative"
        }
        $known = @($entry.sha256)
        foreach ($hash in @($entry.known_sha256)) {
            if ($hash -notmatch '^[0-9a-fA-F]{64}$') { throw '历史 SHA-256 白名单错误。' }
            $known += $hash
        }
        $plan += [PSCustomObject]@{ Path=$relative; Source=$source; Hash=$entry.sha256; Known=$known;
            Distribution=$distribution; OtherPath=$otherPath; OtherHashes=$otherHashes }
    }
    return $plan
}

function Assert-PartyOtherDistributionAbsent([string]$Root, [object[]]$Entries) {
    # 同产品只保留一个入口。独立版遇到任何同名ASI都拒绝，避免顺手删除未知文件。
    # ASI与UAL共用根XInput，因此仅识别本产品的根DLL；未知根DLL允许但从不改动。
    # 跨分发不隐式迁移文件或设置，必须先用原分发卸载入口，再装目标分发。
    $entry = $Entries[0]
    $other = Join-Path $Root $entry.OtherPath
    Assert-PartyPlainPath $other
    if (-not (Test-Path -LiteralPath $other)) { return }
    if ($entry.Distribution -eq 'Standalone') { throw '检测到同名队伍ASI入口；请先卸载原分发，不能混装。' }
    if (-not (Test-Path -LiteralPath $other -PathType Leaf)) { throw '根xinput1_4.dll不是普通文件，未改动文件。' }
    $hash = (Get-FileHash -LiteralPath $other -Algorithm SHA256).Hash
    if ($entry.OtherHashes -contains $hash) { throw '检测到队伍独立版入口；请先卸载独立版再安装ASI，不能混装。' }
}

function Get-PartyExistingHash([string]$Target, [object]$Entry) {
    Assert-PartyPlainPath $Target
    if (-not (Test-Path -LiteralPath $Target)) { return $null }
    if (-not (Test-Path -LiteralPath $Target -PathType Leaf)) { throw "目标不是普通文件：$Target" }
    $hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash
    if ($Entry.Known -notcontains $hash) { throw "同名文件归属未知，保留原文件并中止：$Target" }
    return $hash
}

function Enter-PartyInstallMutex([string]$Root) {
    # 同一游戏目录的合作安装器串行执行；外部程序仍需用户避免同时修改目录。
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $id = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Root.ToLowerInvariant()))).Replace('-', '') }
    finally { $sha.Dispose() }
    $mutex = [Threading.Mutex]::new($false, "Local\Sky2PartyEditor-$id")
    try {
        try { $acquired = $mutex.WaitOne(0) }
        catch [Threading.AbandonedMutexException] { $acquired = $true }
        if (-not $acquired) { throw '该目录已有队伍 Mod 安装或卸载操作正在进行。' }
        return $mutex
    } catch { $mutex.Dispose(); throw }
}

function Write-PartyFile([string]$Target, [object]$Entry) {
    # 临时文件和目标位于同一目录，通过原子重命名提交，避免复制中断损坏现有插件。
    Assert-PartyGameStopped
    $before = Get-PartyExistingHash $Target $Entry
    if ($before -eq $Entry.Hash) { return }
    $directory = Split-Path -Parent $Target
    Assert-PartyPlainPath $directory
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $temporary = Join-Path $directory ('.Sky2PartyEditor-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item -LiteralPath $Entry.Source -Destination $temporary
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $Entry.Hash) { throw '暂存文件校验失败。' }
        Assert-PartyGameStopped
        $current = Get-PartyExistingHash $Target $Entry
        if ($current -ne $before) { throw "文件已被其他进程修改，操作中止：$Target" }
        # PowerShell 会把字符串参数位置的 $null 转成空串；显式传 .NET null，
        # 表示不创建备份路径，避免正常升级在 File.Replace 中报“路径为空”。
        # 与宝箱安装器一致，仅忽略非关键元数据合并失败；文件归属和内容仍已严格复核。
        if ($current) { [IO.File]::Replace($temporary, $Target, [NullString]::Value, $true) }
        else { [IO.File]::Move($temporary, $Target) }
    } finally {
        # 只清理本次创建的随机临时文件，不扫描或删除其他临时文件。
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary }
    }
}
