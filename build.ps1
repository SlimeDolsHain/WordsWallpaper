param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [ValidateSet("x64", "x86")]
    [string]$Arch = "x64",
    [string]$Compiler = "",
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root ("build\" + $Arch)
if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item $buildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

function Find-Gxx {
    param([string]$hint, [string]$arch)
    if ($hint) {
        if (-not (Test-Path $hint)) { throw "找不到编译器: $hint" }
        return (Resolve-Path $hint).Path
    }

    if ($arch -eq "x86") {
        $cmd = Get-Command i686-w64-mingw32-g++ -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
        $candidates = @(
            (Join-Path $root "..\.tools\mingw32\mingw32\bin\i686-w64-mingw32-g++.exe"),
            (Join-Path $root "..\.tools\mingw32\mingw32\bin\g++.exe"),
            (Join-Path $root "..\.tools\mingw32\bin\i686-w64-mingw32-g++.exe"),
            (Join-Path $root "..\.tools\mingw32\bin\g++.exe"),
            "C:\msys64\mingw32\bin\i686-w64-mingw32-g++.exe",
            "C:\msys64\mingw32\bin\g++.exe",
            "C:\mingw32\bin\i686-w64-mingw32-g++.exe",
            "C:\Program Files (x86)\mingw-w64\i686-8.1.0-posix-dwarf-rt_v6-rev0\mingw32\bin\g++.exe"
        )
        foreach ($c in $candidates) {
            if (Test-Path $c) { return $c }
        }
        throw "未找到 32 位 g++.exe。请安装 i686 MinGW-w64，或用 -Compiler 指定 i686-w64-mingw32-g++.exe。"
    }

    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        "D:\JetBrains\CLion 2025.1.1\bin\mingw\bin\g++.exe",
        "C:\msys64\mingw64\bin\g++.exe",
        "C:\mingw64\bin\g++.exe",
        "C:\Program Files\mingw-w64\mingw64\bin\g++.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    throw "未找到 64 位 g++.exe。请安装 MinGW-w64，或用 -Compiler 参数指定 g++ 路径。"
}

$gxx = Find-Gxx $Compiler $Arch
$binDir = Split-Path -Parent $gxx
$windres = Join-Path $binDir "windres.exe"
$env:PATH = "$binDir;$env:PATH"
if (-not (Test-Path $windres)) { throw "找不到 windres.exe（应与 g++ 同目录）" }

$target = (& $gxx -dumpmachine) 2>$null
if ($Arch -eq "x86" -and $target -notmatch "i686") {
    throw "所选编译器不是 32 位 i686 工具链（当前: $target）"
}
if ($Arch -eq "x64" -and $target -notmatch "x86_64") {
    throw "所选编译器不是 64 位 x86_64 工具链（当前: $target）"
}

$src = Join-Path $root "main.cpp"
# Windows 7 API level: runs on Windows 7 / 8 / 8.1 / 10 / 11.
$common = @(
    "-std=c++17",
    "-Wall", "-Wextra",
    "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
    "-DUNICODE", "-D_UNICODE",
    "-DWINVER=0x0601", "-D_WIN32_WINNT=0x0601", "-DNTDDI_VERSION=0x06010000"
)
if ($Config -eq "Release") {
    $common += @("-O2", "-s")
} else {
    $common += @("-O0", "-g")
}
$linkWin7 = @(
    "-Wl,--major-os-version=6", "-Wl,--minor-os-version=1",
    "-Wl,--major-subsystem-version=6", "-Wl,--minor-subsystem-version=1"
)
$libs = @(
    "-static", "-static-libgcc", "-static-libstdc++",
    "-lgdiplus", "-lgdi32", "-luser32", "-lcomdlg32",
    "-lcomctl32", "-lole32", "-lshell32", "-luuid"
)

Push-Location $root
try {
    if ($Test) {
        $out = Join-Path $root ($(if ($Arch -eq "x64") { "WordsWallpaperTest.exe" } else { "WordsWallpaperTest-x86.exe" }))
        Write-Host "Compiling test harness ($Arch, $target)..."
        & $gxx @common @linkWin7 "-DWW_TEST" "-municode" "-o" $out $src @libs
        if ($LASTEXITCODE -ne 0) { throw "compile failed" }
        Write-Host "Built: $out"
    } else {
        Write-Host "Compiling resources ($Arch)..."
        $resObj = Join-Path $buildDir "resource.o"
        & $windres "--preprocessor=gcc.exe" "--preprocessor-arg=-E" "--preprocessor-arg=-xc" "--preprocessor-arg=-DRC_INVOKED" "--codepage=65001" "-O" "coff" "-o" $resObj (Join-Path $root "resource.rc")
        if ($LASTEXITCODE -ne 0) { throw "windres failed" }

        $releaseName = if ($Arch -eq "x64") { "WordsWallpaper-x64.exe" } else { "WordsWallpaper-x86.exe" }
        $debugName   = if ($Arch -eq "x64") { "WordsWallpaperDebug.exe" } else { "WordsWallpaperDebug-x86.exe" }
        $name = if ($Config -eq "Release") { $releaseName } else { $debugName }
        $out = Join-Path $root $name
        Write-Host "Compiling $name ($Arch, $target)..."
        & $gxx @common @linkWin7 "-municode" "-mwindows" "-o" $out $src $resObj @libs
        if ($LASTEXITCODE -ne 0) { throw "compile failed" }

        if ($Config -eq "Release" -and $Arch -eq "x64") {
            Copy-Item $out (Join-Path $root "WordsWallpaper.exe") -Force
        }

        $fi = Get-Item $out
        Write-Host ""
        Write-Host ("Built: {0}" -f $out)
        Write-Host ("Size : {0:N0} bytes ({1:N1} KB)" -f $fi.Length, ($fi.Length / 1KB))
        if ($Config -eq "Release" -and $Arch -eq "x64") {
            Write-Host ("Copy : {0}" -f (Join-Path $root "WordsWallpaper.exe"))
        }
    }
}
finally {
    Pop-Location
}
