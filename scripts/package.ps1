<#
.SYNOPSIS
    构建 CadGeom 并产出发布用的压缩包。

.DESCRIPTION
    .github/workflows/release.yml 里那一步跑的就是这个脚本，没有第二条路径 ——
    所以「推标签之前先在本地跑一遍」是真的在验证同一件事，而不是验证一个长得
    像它的东西。

    产出两个包，放在 -OutDir（默认 dist/）：

      CadGeom-<ver>-windows-x64.zip      二进制包。cadgeom.dll、导入库、公共
                                         头文件、find_package(CadGeom) 用的
                                         cmake 配置、两个示例程序，外加它们的
                                         运行时依赖（MSVC 运行时、Qt）。
      CadGeom-<ver>-src-with-submodules.zip
                                         源码包。GitHub 自动生成的那份不含
                                         external/ 下的子模块，克隆不下来就
                                         build 不了；这一份含。

    vulkan-1.dll 不在包里：它属于显卡驱动。没有驱动的机器带上 loader 也跑不起来，
    带上反而可能盖掉系统里更新的那个。

.EXAMPLE
    # 本地完整跑一遍，和 CI 做的事情一样
    pwsh scripts/package.ps1 -QtPrefix D:/Qt/5.15.2/msvc2019_64

.EXAMPLE
    # 只想看打包这一段，跳过测试
    pwsh scripts/package.ps1 -SkipTests
#>
[CmdletBinding()]
param(
    # 包名里的版本号，可带可不带前导 v。不给就用 CMakeLists.txt 里的 project(VERSION)。
    [string] $Version,

    # 打包用的构建配置。Release 出干净的包，RelWithDebInfo 会额外带上 cadgeom.pdb。
    [ValidateSet('Release', 'RelWithDebInfo', 'MinSizeRel', 'Debug')]
    [string] $Config = 'Release',

    [string] $BuildDir = 'build-release',
    [string] $OutDir   = 'dist',

    # Qt 的安装前缀，例如 D:/Qt/5.15.2/msvc2019_64。留空则不构建 qt_viewer。
    [string] $QtPrefix = '',

    [string] $Generator = 'Visual Studio 17 2022',

    [switch] $SkipTests,
    [switch] $SkipSourceArchive,

    # 允许标签版本和 project(VERSION) 对不上。正常不该用到 —— 见 Assert-Version。
    [switch] $AllowVersionMismatch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repo = Split-Path -Parent $PSScriptRoot
Push-Location $Repo

# ---------------------------------------------------------------------------
# 小工具
# ---------------------------------------------------------------------------

function Write-Step {
    param([string] $Text)
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

# 原生命令的退出码不会自己变成异常，逐条查。
function Invoke-Native {
    param([string] $Exe, [string[]] $Arguments)
    Write-Host "    $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe 退出码 $LASTEXITCODE"
    }
}

function Get-ProjectVersion {
    $line = Select-String -Path (Join-Path $Repo 'CMakeLists.txt') `
                          -Pattern '^\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$' |
            Select-Object -First 1
    if (-not $line) { throw '在 CMakeLists.txt 里没找到 project(VERSION ...)' }
    return $line.Matches[0].Groups[1].Value
}

# 包名和 CadGeomConfigVersion.cmake 必须说同一个版本号，否则 find_package 的
# 版本检查会对着一个 0.1.0 的包放行一个要求 0.2.0 的宿主。标签允许带后缀
# （v0.2.0-rc1），数字部分必须一致。
function Assert-Version {
    param([string] $Requested, [string] $Project)
    if ($Requested -eq $Project) { return }

    $m = [regex]::Match($Requested, '^([0-9]+\.[0-9]+\.[0-9]+)')
    if (-not $m.Success) {
        throw "版本号 '$Requested' 不是 MAJOR.MINOR.PATCH[后缀] 的形式"
    }
    $core = $m.Groups[1].Value
    if ($core -eq $Project) { return }

    $msg = "版本号对不上：要打的是 $Requested，CMakeLists.txt 里 project(VERSION) 写的是 $Project。" +
           "先把 project(VERSION) 改成 $core 再打标签。"
    if ($AllowVersionMismatch) { Write-Warning $msg } else { throw $msg }
}

# ---------------------------------------------------------------------------

try {
    $projectVersion = Get-ProjectVersion

    if ([string]::IsNullOrWhiteSpace($Version)) {
        $Version = $projectVersion
    } else {
        $Version = $Version -replace '^v', ''
    }
    Assert-Version -Requested $Version -Project $projectVersion

    $outAbs = Join-Path $Repo $OutDir
    if (Test-Path $outAbs) { Remove-Item $outAbs -Recurse -Force }
    New-Item -ItemType Directory -Path $outAbs -Force | Out-Null

    Write-Host "CadGeom $Version  ($Config)"
    Write-Host "  repo   : $Repo"
    Write-Host "  build  : $BuildDir"
    Write-Host "  out    : $outAbs"
    Write-Host "  qt     : $(if ($QtPrefix) { $QtPrefix } else { '(无 —— 不构建 qt_viewer)' })"
    Write-Host "  vulkan : $(if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { '(无 —— 会打出一个没有渲染器的包)' })"

    if (-not $env:VULKAN_SDK) {
        Write-Warning '没有 VULKAN_SDK。构建会自己丢掉渲染器，打出来的 cadgeom.dll 里 CreateViewport 只会返回 NotSupported。'
    }

    # -- 子模块 ----------------------------------------------------------
    # CI 的 checkout 已经带 recursive 了，这一步是给本地兜底的。

    Write-Step '同步子模块'
    Invoke-Native git @('submodule', 'update', '--init', '--recursive')

    # -- 配置 ------------------------------------------------------------

    Write-Step '配置'
    $cmakeArgs = @(
        '-S', '.',
        '-B', $BuildDir,
        '-G', $Generator
    )
    # -A 只有 Visual Studio 生成器认，给 Ninja 传会直接报错。
    if ($Generator -like 'Visual Studio*') { $cmakeArgs += @('-A', 'x64') }
    $cmakeArgs += @(
        "-DCADGEOM_PACKAGE_VERSION=$Version",
        '-DCADGEOM_BUILD_TESTS=ON',
        '-DCADGEOM_BUILD_EXAMPLES=ON',
        '-DCADGEOM_INSTALL_EXAMPLES=ON',
        '-DCADGEOM_WARNINGS_AS_ERRORS=ON'
    )
    if ($QtPrefix) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPrefix" }
    Invoke-Native cmake $cmakeArgs

    # -- 构建 ------------------------------------------------------------

    Write-Step "构建 ($Config)"
    Invoke-Native cmake @('--build', $BuildDir, '--config', $Config, '--parallel')

    # -- 测试 ------------------------------------------------------------
    # 测试是 Vulkan-free 的（docs/architecture.md §8），所以这一步在没有 SDK 的
    # 机器上也照跑。

    if ($SkipTests) {
        Write-Step '测试（已跳过）'
    } else {
        Write-Step '测试'
        Invoke-Native ctest @('--test-dir', $BuildDir, '-C', $Config, '--output-on-failure')
    }

    # -- 二进制包 --------------------------------------------------------

    Write-Step '打二进制包'
    Invoke-Native cpack @(
        '--config', (Join-Path $BuildDir 'CPackConfig.cmake'),
        '-C', $Config,
        '-G', 'ZIP',
        '-B', $outAbs
    )
    # cpack 的暂存目录，不要留在交付目录里。
    $staging = Join-Path $outAbs '_CPack_Packages'
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }

    # -- 源码包 ----------------------------------------------------------
    #
    # GitHub 自动生成的 Source code (zip) 里 external/ 是七个空目录 —— 子模块
    # 不在归档里。这一份用 git ls-files --recurse-submodules 列出所有被跟踪的
    # 文件（含子模块内容），直接写进 zip，不经过中间目录。

    if ($SkipSourceArchive) {
        Write-Step '源码包（已跳过）'
    } else {
        Write-Step '打源码包（含子模块）'
        $srcName = "CadGeom-$Version-src-with-submodules"
        $srcZip  = Join-Path $outAbs "$srcName.zip"

        $files = & git -c core.quotepath=off ls-files --recurse-submodules
        if ($LASTEXITCODE -ne 0) { throw "git ls-files 退出码 $LASTEXITCODE" }
        Write-Host "    $($files.Count) 个文件 -> $srcName.zip" -ForegroundColor DarkGray

        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::Open(
                   $srcZip, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($f in $files) {
                if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { continue }
                $full = (Resolve-Path -LiteralPath $f).Path
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $zip, $full, "$srcName/$f",
                    [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        } finally {
            $zip.Dispose()
        }
    }

    # -- 校验和 ----------------------------------------------------------
    # 一份汇总，格式和 sha256sum -c 认的一样。

    Write-Step '校验和'
    $sums = Get-ChildItem $outAbs -Filter *.zip | Sort-Object Name | ForEach-Object {
        "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"
    }
    $sums | Set-Content -Path (Join-Path $outAbs 'SHA256SUMS.txt') -Encoding ascii
    $sums | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }

    # -- 自检 ------------------------------------------------------------
    #
    # 打出一个缺了渲染器、或者缺了 Qt 运行时的包，zip 本身是「成功」的 —— 所以
    # 这里对着内容再问一遍，缺什么就在日志里说出来。

    Write-Step '包内容自检'
    $binZip = Get-ChildItem $outAbs -Filter "CadGeom-$Version-windows-*.zip" | Select-Object -First 1
    if (-not $binZip) { throw '没有找到二进制包' }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $names = @()
    $z = [System.IO.Compression.ZipFile]::OpenRead($binZip.FullName)
    try { $names = $z.Entries | ForEach-Object { $_.FullName } } finally { $z.Dispose() }

    # 必须有，没有就是打包坏了。
    $required = @(
        'bin/cadgeom.dll',
        'bin/glfw_viewer.exe',
        'lib/cadgeom.lib',
        'lib/cmake/CadGeom/CadGeomConfig.cmake',
        'include/cadgeom/CadGeom.h',
        'include/cadgeom/Export.h'
    )
    # 少了只是包不够完整，日志里点名，不拦。
    $expected = @{
        'bin/qt_viewer.exe'   = 'Qt 示例（没给 -QtPrefix 就没有）'
        'bin/mfc_viewer.exe'  = 'MFC 示例（构建机没装 VS 的 MFC 组件就没有）'
        'bin/platforms/'      = 'Qt 平台插件'
        'bin/msvcp140.dll'    = 'MSVC 运行时'
    }

    $missing = @()
    foreach ($r in $required) {
        if ($names -notcontains "CadGeom-$Version-windows-x64/$r") { $missing += $r }
    }
    if ($missing.Count -gt 0) {
        throw "包里少了必须有的东西：`n  $($missing -join "`n  ")"
    }
    Write-Host "    必需内容齐全（$($names.Count) 个条目）" -ForegroundColor DarkGray

    foreach ($k in $expected.Keys) {
        $hit = $names | Where-Object { $_ -like "CadGeom-$Version-windows-x64/$k*" } | Select-Object -First 1
        if ($hit) {
            Write-Host "    有  $k" -ForegroundColor DarkGray
        } else {
            Write-Warning "包里没有 $k —— $($expected[$k])"
        }
    }

    # -- 结果 ------------------------------------------------------------

    Write-Step '完成'
    Get-ChildItem $outAbs -File | Sort-Object Name | ForEach-Object {
        '{0,10:N1} MB  {1}' -f ($_.Length / 1MB), $_.Name
    }
}
finally {
    Pop-Location
}
