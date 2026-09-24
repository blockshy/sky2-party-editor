<#
.SYNOPSIS
准备文件哈希锁定的 MinHook 与 ImGui 最小源码快照。
.DESCRIPTION
可分别指定干净的本地 Git 仓库离线导出；没有来源时才下载精确提交。
已有快照只验证，不覆盖修改。不处理游戏目录或玩家存档。
#>
[CmdletBinding()]
param([string]$Destination, [string]$LocalSource, [string]$ImguiLocalSource)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $Destination) { $Destination = Join-Path $projectRoot '.deps' }
$lock = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'dependencies.json') | ConvertFrom-Json

function Assert-DependencySnapshot([string]$Directory, $Entry, [string]$Name) {
    Assert-PartyPlainPath $Directory
    $expected = @($Entry.files.PSObject.Properties.Name)
    foreach ($property in $Entry.files.PSObject.Properties) {
        # 清单必须是普通相对路径，防止损坏的锁文件把导出范围带出依赖目录。
        if ($property.Name -match '(^[/\\])|(^|[/\\])\.\.([/\\]|$)|:' -or
            $property.Value -notmatch '^[0-9a-f]{64}$') { throw "$Name 文件白名单无效。" }
        $path = Join-Path $Directory $property.Name
        Assert-PartyPlainPath $path
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $property.Value) {
            throw "$Name 文件与锁定版本不同，未覆盖：$path"
        }
    }
    $actual = @(Get-ChildItem -LiteralPath $Directory -File -Recurse -Force | ForEach-Object {
        $_.FullName.Substring($Directory.TrimEnd('\', '/').Length + 1).Replace('\', '/')
    })
    if (@(Compare-Object $expected $actual).Count -ne 0) { throw "$Name 快照包含缺失或额外文件，未覆盖。" }
}

Assert-PartyPlainPath $Destination
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$dependencyRoot = (Resolve-Path -LiteralPath $Destination).Path
$dependencies = @(
    @{ Name='minhook'; Repository='https://github.com/TsudaKageyu/minhook.git'; Source=$LocalSource },
    @{ Name='imgui'; Repository='https://github.com/ocornut/imgui.git'; Source=$ImguiLocalSource }
)
foreach ($dependency in $dependencies) {
    $name = $dependency.Name
    $entry = $lock.build.$name
    if ($entry.commit -notmatch '^[0-9a-f]{40}$' -or $entry.repository -cne $dependency.Repository) {
        throw "$name 依赖锁定信息无效。"
    }
    $target = Join-Path $dependencyRoot $name
    if (Test-Path -LiteralPath $target) {
        Assert-DependencySnapshot $target $entry $name
        Write-Output "已核对 $name $($entry.commit)，无需下载。"
        continue
    }
    Get-Command git -ErrorAction Stop | Out-Null
    if ($dependency.Source) {
        Assert-PartyPlainPath $dependency.Source
        $repository = (Resolve-Path -LiteralPath $dependency.Source).Path
        $head = & git -C $repository rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $head -ne $entry.commit) { throw "本地 $name 提交不匹配。" }
        $changes = & git -C $repository status --porcelain
        if ($LASTEXITCODE -ne 0 -or $changes) { throw "本地 $name 工作区有修改，未复制。" }
    } else {
        $repository = Join-Path $dependencyRoot ('.download-' + $name + '-' + [Guid]::NewGuid().ToString('N'))
        & git init --bare $repository
        if ($LASTEXITCODE -ne 0) { throw "无法创建 $name 下载缓存。" }
        & git -C $repository fetch --depth 1 $entry.repository $entry.commit
        if ($LASTEXITCODE -ne 0) { throw '依赖下载失败；缓存保留以便排查。' }
    }
    $id = [Guid]::NewGuid().ToString('N')
    $archive = Join-Path $dependencyRoot ('.setup-' + $name + '-' + $id + '.zip')
    $stage = Join-Path $dependencyRoot ('.setup-' + $name + '-' + $id)
    $paths = @($entry.files.PSObject.Properties.Name)
    # 从 Git 对象导出原始字节，避免不同 core.autocrlf 配置改变源码指纹。
    & git -c core.autocrlf=false -c core.eol=lf -C $repository archive --format=zip "--output=$archive" $entry.commit -- @paths
    if ($LASTEXITCODE -ne 0) { throw "无法导出锁定的 $name 源码。" }
    Expand-Archive -LiteralPath $archive -DestinationPath $stage
    Assert-DependencySnapshot $stage $entry $name
    # 移动前检查最终绝对路径及链接祖先，所有操作保持在同一 PowerShell 中完成。
    $boundary = [IO.Path]::GetFullPath($dependencyRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    foreach ($path in @($stage, $target)) {
        $absolute = [IO.Path]::GetFullPath($path)
        if (-not $absolute.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw '快照目标越过依赖目录。' }
        Assert-PartyPlainPath $absolute
    }
    if (Test-Path -LiteralPath $target) { throw '准备过程中目标目录被创建，未覆盖。' }
    Move-Item -LiteralPath $stage -Destination $target
    Remove-Item -LiteralPath $archive
    Write-Output "已准备 $name $($entry.commit)：$target"
}
