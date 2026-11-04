<#
.SYNOPSIS
  Builds TenChar Desktop for Windows (x64) in one command: prerequisites, configure, build, test,
  deploy the Qt runtime, optionally sign, zip and checksum.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File windows\build.ps1 -InstallPrereqs
  First run on a clean PC: installs what is missing (Visual Studio Build Tools, Git, Python through winget;
  Qt through aqtinstall; OpenSSL through vcpkg), then builds. Run from an administrator shell the first time.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File windows\build.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
  Use a Qt you already installed. Nothing else is installed unless you also pass -InstallPrereqs.

Output: dist\TenChar-win64\ (run tenchar.exe from there), dist\TenChar-win64.zip, dist\TenChar-win64.zip.sha256,
        dist\BUILD-INFO.txt. NOT tested on Windows by the author: if a step fails, the message names it.
#>
[CmdletBinding()]
param(
    [switch]$InstallPrereqs,                 # install what is missing
    [string]$QtDir = '',                     # e.g. C:\Qt\6.8.3\msvc2022_64 (a Qt 6.4 or newer MSVC kit)
    [string]$QtSpec = '6.8',                 # which Qt to fetch when none is found (a 6.8 LTS release)
    [string]$Triplet = 'x64-windows-static-md',  # OpenSSL linked statically, dynamic CRT (matches Qt). Fallback: x64-windows
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Config = 'Release',
    [string]$DepsDir = '',                   # where downloaded Qt and vcpkg live (default: <repo>\.deps)
    [string]$OutDir = '',                    # where the package goes (default: <repo>\dist)
    [switch]$Clean,                          # delete the build folder first
    [switch]$SkipTests,
    [switch]$Smoke,                          # also launch the app headless and screenshot a few screens
    [string]$SignThumbprint = ''             # sign tenchar.exe with this certificate (needs signtool from the Windows SDK)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Repo = Split-Path -Parent $PSScriptRoot
if (-not $DepsDir) { $DepsDir = Join-Path $Repo '.deps' }
if (-not $OutDir) { $OutDir = Join-Path $Repo 'dist' }
$Build = Join-Path $Repo 'build'
New-Item -ItemType Directory -Force -Path $DepsDir, $OutDir | Out-Null

function Step([string]$Message) { Write-Host ''; Write-Host "==> $Message" -ForegroundColor Cyan }

function Invoke-Checked {
    param([Parameter(Mandatory)][string]$File, [string[]]$ArgumentList = @())
    & $File @ArgumentList
    if ($LASTEXITCODE -ne 0) { throw "'$File' failed with exit code $LASTEXITCODE" }
}

function Update-SessionPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Install-WithWinget {
    param([Parameter(Mandatory)][string]$Id, [string]$Override = '')
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget is not available, so '$Id' cannot be installed automatically. Install it by hand and run this script again."
    }
    $a = @('install', '--id', $Id, '-e', '--accept-package-agreements', '--accept-source-agreements', '--silent')
    if ($Override) { $a += @('--override', $Override) }
    & winget @a
    # -1978335189 (0x8A15002B) means "already installed, nothing to upgrade"
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { throw "winget could not install $Id (exit code $LASTEXITCODE)" }
    Update-SessionPath
}

# ---- Visual Studio (compiler, Windows SDK, CMake, Ninja) --------------------------------------------------
function Get-VsPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $p = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($p) { return ($p | Select-Object -First 1) }
    return $null
}

function Import-VsEnvironment([string]$VsPath) {
    # Run VsDevCmd.bat in a throwaway cmd file and copy the environment it sets up (PATH, INCLUDE, LIB, ...).
    $bat = Join-Path $VsPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $bat)) { throw "VsDevCmd.bat not found under $VsPath" }
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("tenchar-vsenv-" + $PID + ".cmd")
    Set-Content -Path $tmp -Encoding ASCII -Value ("@echo off`r`ncall `"$bat`" -arch=x64 -host_arch=x64 -no_logo`r`nset")
    $lines = & cmd.exe /c $tmp
    Remove-Item $tmp -ErrorAction SilentlyContinue
    foreach ($l in $lines) {
        # SetEnvironmentVariable, not Set-Item env:, because names such as ProgramFiles(x86) contain parentheses
        if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
}

Step 'Visual Studio (C++ compiler)'
$vs = Get-VsPath
if (-not $vs) {
    if (-not $InstallPrereqs) { throw 'Visual Studio 2022 with the "Desktop development with C++" workload was not found. Re-run with -InstallPrereqs, or install it yourself.' }
    Write-Host 'Installing Visual Studio 2022 Build Tools (this takes a while and needs an administrator shell)...'
    Install-WithWinget -Id 'Microsoft.VisualStudio.2022.BuildTools' -Override '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    $vs = Get-VsPath
    if (-not $vs) { throw 'Visual Studio Build Tools did not install correctly. Install the "Desktop development with C++" workload by hand.' }
}
Import-VsEnvironment $vs
Write-Host "using $vs"
foreach ($tool in 'cmake', 'ninja', 'cl') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        if ($InstallPrereqs -and $tool -eq 'cmake') { Install-WithWinget -Id 'Kitware.CMake' }
        elseif ($InstallPrereqs -and $tool -eq 'ninja') { Install-WithWinget -Id 'Ninja-build.Ninja' }
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "'$tool' was not found on PATH after setting up Visual Studio." }
    }
}

# ---- Python (only needed to fetch Qt and for the contrast test) ----------------------------------------------
function Get-Python {
    foreach ($c in 'python', 'py') {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notlike '*WindowsApps*') {   # the Microsoft Store stub is not a real Python
            if ($c -eq 'py') { return @('py', '-3') }
            return @('python')
        }
    }
    return $null
}
function Invoke-Python([string[]]$PyArgs, [switch]$Capture) {
    $py = Get-Python
    if (-not $py) { throw 'Python 3 was not found.' }
    $pre = @()
    if ($py.Count -gt 1) { $pre = $py[1..($py.Count - 1)] }
    if ($Capture) { return (& $py[0] @($pre + $PyArgs)) }
    Invoke-Checked $py[0] ($pre + $PyArgs)
}

# ---- Qt 6 ----------------------------------------------------------------------------------------------------
function Find-Qt {
    if ($QtDir) {
        if (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6')) { return (Resolve-Path $QtDir).Path }
        throw "-QtDir '$QtDir' does not look like a Qt 6 kit (no lib\cmake\Qt6)."
    }
    $candidates = @()
    foreach ($root in @((Join-Path $DepsDir 'Qt'), 'C:\Qt', (Join-Path $env:USERPROFILE 'Qt'))) {
        if (Test-Path $root) { $candidates += Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue | ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -Filter 'msvc2022_64' -ErrorAction SilentlyContinue } }
    }
    $candidates = @($candidates | Where-Object { Test-Path (Join-Path $_.FullName 'lib\cmake\Qt6') })
    if ($candidates.Count -eq 0) { return $null }
    return ($candidates | Sort-Object { try { [version]$_.Parent.Name } catch { [version]'0.0' } } | Select-Object -Last 1).FullName
}

Step 'Qt 6'
$Qt = Find-Qt
if (-not $Qt) {
    if (-not $InstallPrereqs) { throw 'Qt 6 (MSVC 2022 64-bit kit) was not found. Pass -QtDir, or re-run with -InstallPrereqs to download it with aqtinstall.' }
    if (-not (Get-Python)) { Install-WithWinget -Id 'Python.Python.3.12' }
    Write-Host "Downloading Qt $QtSpec with aqtinstall (a third-party downloader; Qt itself comes from Qt's servers)..."
    Invoke-Python @('-m', 'pip', 'install', '--user', '--upgrade', 'aqtinstall')
    $ver = (Invoke-Python @('-m', 'aqt', 'list-qt', 'windows', 'desktop', '--spec', $QtSpec, '--latest-version') -Capture | Select-Object -Last 1).ToString().Trim()
    if (-not $ver) { throw "aqtinstall could not find a Qt $QtSpec release." }
    Invoke-Python @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $ver, 'win64_msvc2022_64', '-O', (Join-Path $DepsDir 'Qt'))
    $Qt = Find-Qt
    if (-not $Qt) { throw 'Qt was downloaded but its folder was not found.' }
}
Write-Host "using Qt at $Qt"

# ---- vcpkg + OpenSSL --------------------------------------------------------------------------------------------
function Get-VcpkgRoot {
    foreach ($r in @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT, (Join-Path $DepsDir 'vcpkg'))) {
        if ($r -and (Test-Path (Join-Path $r 'scripts\buildsystems\vcpkg.cmake'))) { return $r }
    }
    return $null
}

Step 'vcpkg (builds OpenSSL 3 from source)'
$Vcpkg = Get-VcpkgRoot
if (-not $Vcpkg) {
    if (-not $InstallPrereqs) { throw 'vcpkg was not found. Set VCPKG_ROOT to an existing vcpkg, or re-run with -InstallPrereqs to clone one.' }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Install-WithWinget -Id 'Git.Git' }
    $Vcpkg = Join-Path $DepsDir 'vcpkg'
    Invoke-Checked 'git' @('clone', '--depth', '1', 'https://github.com/microsoft/vcpkg', $Vcpkg)
    Invoke-Checked (Join-Path $Vcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
}
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $DepsDir 'vcpkg-archives'
New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
Write-Host "using vcpkg at $Vcpkg"

# ---- configure, build, test -----------------------------------------------------------------------------------
if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

function Invoke-Configure([string[]]$Extra) {
    $cfg = @('-G', 'Ninja', '-S', $Repo, '-B', $Build, "-DCMAKE_BUILD_TYPE=$Config",
             "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $Vcpkg 'scripts\buildsystems\vcpkg.cmake')",
             "-DVCPKG_TARGET_TRIPLET=$Triplet", "-DVCPKG_INSTALLED_DIR=$(Join-Path $DepsDir 'vcpkg_installed')",
             "-DCMAKE_PREFIX_PATH=$Qt", '-DTC_BUILD_APP=ON')
    Invoke-Checked 'cmake' ($cfg + $Extra)
}

Step 'Configuring (the first run builds OpenSSL, which takes several minutes)'
Invoke-Configure @('-DTC_DEV_TOOLS=OFF')
Step 'Building'
Invoke-Checked 'cmake' @('--build', $Build)

$savedPath = $env:Path
$env:Path = (Join-Path $Qt 'bin') + ';' + $env:Path     # so the test programs find Qt's DLLs

if (-not $SkipTests) {
    Step 'Testing'
    $env:QT_QPA_PLATFORM = 'offscreen'
    try { Invoke-Checked 'ctest' @('--test-dir', $Build, '--output-on-failure', '-j', '4') }
    finally { Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
}

# ---- deploy and package ------------------------------------------------------------------------------------------
Step 'Collecting the Qt runtime (windeployqt)'
$Stage = Join-Path $OutDir 'TenChar-win64'
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item (Join-Path $Build 'app\tenchar.exe') $Stage
Invoke-Checked (Join-Path $Qt 'bin\windeployqt.exe') @('--release', '--qmldir', (Join-Path $Repo 'app\qml'), '--no-translations', '--compiler-runtime', (Join-Path $Stage 'tenchar.exe'))

$notices = @"
TenChar Desktop - third-party components

Qt 6 (LGPLv3, linked dynamically: the Qt DLLs in this folder may be replaced with your own build). https://www.qt.io/ - source code of Qt is available from Qt.
OpenSSL 3 (Apache License 2.0, linked statically). https://www.openssl.org/
Inter (SIL Open Font License 1.1) and Noto Sans Bengali (SIL Open Font License 1.1): licence texts are in the licenses folder.
Read Qt's licence terms before any public release.
"@
Set-Content -Path (Join-Path $Stage 'THIRD-PARTY-NOTICES.txt') -Value $notices -Encoding UTF8
$licDir = Join-Path $Stage 'licenses'
New-Item -ItemType Directory -Force -Path $licDir | Out-Null
Copy-Item (Join-Path $Repo 'app\resources\fonts\*LICENSE.txt') $licDir

if ($SignThumbprint) {
    Step 'Signing'
    $signtool = Get-ChildItem -Path (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\*\x64\signtool.exe') -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
    if (-not $signtool) { throw 'signtool.exe not found (install the Windows SDK).' }
    Invoke-Checked $signtool.FullName @('sign', '/sha1', $SignThumbprint, '/fd', 'SHA256', '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', (Join-Path $Stage 'tenchar.exe'))
}

Step 'Zipping and checksum'
$Zip = Join-Path $OutDir 'TenChar-win64.zip'
if (Test-Path $Zip) { Remove-Item $Zip }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Zip
$hash = (Get-FileHash -Path $Zip -Algorithm SHA256).Hash.ToLower()
Set-Content -Path "$Zip.sha256" -Value "$hash *TenChar-win64.zip" -Encoding ASCII

$commit = 'unknown'
if ((Test-Path (Join-Path $Repo '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) { $commit = (& git -C $Repo rev-parse HEAD) }
$clVersion = (& cmd.exe /c "cl 2>&1" | Select-Object -First 1)   # cl prints its banner on stderr
@(
    "TenChar Desktop build",
    "date:      $((Get-Date).ToUniversalTime().ToString('s'))Z",
    "commit:    $commit",
    "config:    $Config",
    "qt:        $Qt",
    "compiler:  $clVersion",
    "vcpkg:     $Vcpkg  (triplet $Triplet)",
    "signed:    $([bool]$SignThumbprint)",
    "sha256:    $hash  TenChar-win64.zip"
) | Set-Content -Path (Join-Path $OutDir 'BUILD-INFO.txt') -Encoding UTF8

# ---- optional headless smoke test -------------------------------------------------------------------------------
if ($Smoke) {
    Step 'Smoke test: launching the real app headless and taking screenshots'
    Invoke-Configure @('-DTC_DEV_TOOLS=ON')
    Invoke-Checked 'cmake' @('--build', $Build, '--target', 'tenchar')
    $shots = Join-Path $OutDir 'smoke'
    New-Item -ItemType Directory -Force -Path $shots | Out-Null
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_BACKEND = 'software'
    $exe = Join-Path $Build 'app\tenchar.exe'
    function Invoke-App([string[]]$AppArgs) {
        $p = Start-Process -FilePath $exe -ArgumentList $AppArgs -PassThru -WindowStyle Hidden
        if (-not $p.WaitForExit(90000)) { $p.Kill(); throw "tenchar.exe did not finish: $($AppArgs -join ' ')" }
    }
    try {
        foreach ($name in 'setup', 'unlock', 'pair', 'waiting', 'verify', 'chat', 'settings') {
            $png = Join-Path $shots "$name.png"
            Invoke-App @('--preview', $name, '--grab', $png)
            if (-not (Test-Path $png) -or (Get-Item $png).Length -lt 15000) { throw "smoke: screen '$name' did not render (missing or blank screenshot)" }
            Write-Host "  rendered $name"
        }
        $tree = Join-Path $shots 'accessibility-chat.txt'
        Invoke-App @('--preview', 'chat', '--a11y-dump', $tree)
        $text = ''
        if (Test-Path $tree) { $text = Get-Content $tree -Raw }
        foreach ($needle in 'Lock', 'Settings', 'Send') { if ($text -notmatch [regex]::Escape($needle)) { throw "smoke: the accessibility tree has no '$needle'" } }
        Write-Host '  accessibility tree has named controls'
    }
    finally {
        Remove-Item Env:\QT_QPA_PLATFORM, Env:\QT_QUICK_BACKEND -ErrorAction SilentlyContinue
    }
}

$env:Path = $savedPath
Step 'Done'
Write-Host "Run:       $(Join-Path $Stage 'tenchar.exe')"
Write-Host "Package:   $Zip"
Write-Host "SHA-256:   $hash"
