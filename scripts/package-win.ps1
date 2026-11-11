# Builds the Windows x64 Release and packages it with Inno Setup.
#
#   pwsh scripts/package-win.ps1                 # build, test, package
#   pwsh scripts/package-win.ps1 -SkipTests      # build and package
#   pwsh scripts/package-win.ps1 -SkipBuild      # package the existing build only
#   pwsh scripts/package-win.ps1 -OutputDir dist # put the installer somewhere else
#
# Needs: Visual Studio (Build Tools) with the C++ workload, vcpkg, Inno Setup 6.
# Output: out/installer/BrowserTamer-zh-<version>-win64-setup.exe (or -OutputDir)

[CmdletBinding()]
param(
    # vcpkg checkout; defaults to $env:VCPKG_ROOT, then to the vcpkg found on PATH
    [string]$VcpkgRoot,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    # where the installer goes; relative paths are relative to the repository root
    [string]$OutputDir = 'out/installer'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$preset = 'x64-release'
$buildDir = Join-Path $repo "out/build/$preset"
if (-not [IO.Path]::IsPathRooted($OutputDir)) { $OutputDir = Join-Path $repo $OutputDir }

function Stop-ResidentFromBuild {
    # bt.exe stays resident in the tray, which locks the build output. Ask it to quit through its own
    # hidden window (works with any build, unlike "bt.exe --quit"; taskkill can miss that window).
    $exe = [IO.Path]::GetFullPath((Join-Path $buildDir 'bt.exe'))
    $running = @(Get-Process bt -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
    if (-not $running) { return }
    Add-Type -Namespace BtPackage -Name User32 -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
'@
    $hwnd = [BtPackage.User32]::FindWindowW('Browser Tamer Resident', [NullString]::Value)
    if ($hwnd -ne [IntPtr]::Zero) { [BtPackage.User32]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
    $running | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
    if (Get-Process -Id $running.Id -ErrorAction SilentlyContinue) {
        throw "bt.exe from the build directory is still running; quit it from the tray icon and retry"
    }
    Write-Host "stopped the running bt.exe from the build directory"
}

function Invoke-InVsShell([string]$command) {
    # run a command inside the x64 MSVC developer environment
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: is Visual Studio installed?" }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "no Visual Studio installation with the C++ x64 tools was found" }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && cd /d `"$repo`" && $command"
    if ($LASTEXITCODE -ne 0) { throw "command failed (exit $LASTEXITCODE): $command" }
}

# --- version, from project(... VERSION x.y.z) in CMakeLists.txt
$cmakeLists = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
$version = [regex]::Match($cmakeLists, '(?m)^\s*VERSION\s+(\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw "could not read the project version from CMakeLists.txt" }
Write-Host "Browser Tamer $version" -ForegroundColor Cyan

# --- build
if (-not $SkipBuild) {
    if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $VcpkgRoot) {
        $vcpkgExe = Get-Command vcpkg -ErrorAction SilentlyContinue
        if ($vcpkgExe) { $VcpkgRoot = Split-Path -Parent $vcpkgExe.Source }
    }
    if (-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'))) {
        throw "vcpkg not found: pass -VcpkgRoot or set VCPKG_ROOT"
    }
    Write-Host "vcpkg: $VcpkgRoot"

    # the preset reads VCPKG_ROOT; set it for this process only
    $env:VCPKG_ROOT = $VcpkgRoot
    Write-Host "configuring ($preset)..." -ForegroundColor Cyan
    Invoke-InVsShell "cmake --preset $preset"
    Write-Host "building..." -ForegroundColor Cyan
    Stop-ResidentFromBuild
    Invoke-InVsShell "cmake --build out/build/$preset"

    if (-not $SkipTests) {
        Write-Host "running unit tests..." -ForegroundColor Cyan
        Invoke-InVsShell "ctest --test-dir out/build/$preset --output-on-failure"
    }
}

foreach ($f in 'bt.exe', 'clearurls_rules.json') {
    if (-not (Test-Path (Join-Path $buildDir $f))) { throw "missing $f in $buildDir (build first, or drop -SkipBuild)" }
}

# --- package
$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) not found" }

Write-Host "packaging with Inno Setup..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force $OutputDir | Out-Null
& $iscc /Qp "/DAppVersion=$version" "/DBuildDir=$buildDir" "/O$OutputDir" (Join-Path $repo 'installer/bt.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }

$setup = Join-Path $OutputDir "BrowserTamer-zh-$version-win64-setup.exe"
Write-Host "done: $setup" -ForegroundColor Green
