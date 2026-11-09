<#
.SYNOPSIS
  Builds TenChar Desktop for Windows (x64) in one command: prerequisites, configure, build, test,
  deploy the Qt runtime, optionally sign, zip and checksum.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File windows\build.ps1 -InstallPrereqs
  First run on a clean PC: installs what is missing (Visual Studio Build Tools, Git, Python through winget;
  Qt through aqtinstall; OpenSSL through vcpkg), then builds. Run from an administrator shell the first time.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File windows\build.ps1 -InstallPrereqs -OpenSslRoot "C:\Program Files\OpenSSL-Win64"
  Recommended: use a prebuilt OpenSSL (winget install ShiningLight.OpenSSL.Dev) instead of building one with vcpkg.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File windows\build.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
  Use a Qt you already installed. Nothing else is installed unless you also pass -InstallPrereqs.

Output: dist\TenChar-win64\ (run tenchar.exe from there), dist\TenChar-win64.zip, dist\TenChar-win64.zip.sha256,
        dist\BUILD-INFO.txt. Full steps and a troubleshooting table: docs\BUILD-WINDOWS.md. If a step fails, a diagnostics block is printed: send all of it.
#>
[CmdletBinding()]
param(
    [switch]$InstallPrereqs,                 # install what is missing
    [string]$QtDir = '',                     # e.g. C:\Qt\6.8.3\msvc2022_64 (a Qt 6.4 or newer MSVC kit)
    [string]$QtSpec = '6.8',                 # which Qt to fetch when none is found (a 6.8 LTS release)
    [string]$OpenSslRoot = '',               # use a prebuilt OpenSSL 3 (folder with include\ lib\ bin\) instead of building one with vcpkg
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
    # Add entries that an installer put on the machine/user PATH. Never replace the PATH: the compiler's paths from
    # Visual Studio live only in this process.
    $machineAndUser = ([Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')) -split ';'
    $have = @($env:Path -split ';' | Where-Object { $_ })
    foreach ($entry in $machineAndUser) {
        if ($entry -and ($have -notcontains $entry)) { $env:Path = $env:Path + ';' + $entry; $have += $entry }
    }
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
function Get-VsPath([switch]$AnyWorkload) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    if ($AnyWorkload) {   # any Visual Studio / Build Tools install, even one without the C++ workload
        $p = & $vswhere -all -prerelease -products '*' -property installationPath
    } else {
        $p = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if ($p) { return ($p | Select-Object -First 1) }
    return $null
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
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
    if (-not (Test-Admin)) { throw 'Installing the C++ workload needs an administrator shell. Right-click build-windows.bat and choose "Run as administrator" (or open PowerShell as administrator).' }
    $existing = Get-VsPath -AnyWorkload
    if ($existing) {
        # Visual Studio or Build Tools is registered but WITHOUT a usable C++ workload: winget cannot add a workload to an
        # existing install, so ask the Visual Studio Installer to modify it first.
        Write-Host "Visual Studio is registered at $existing but has no usable C++ workload. Trying to add it..."
        $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
        if (Test-Path $setup) {
            $p = Start-Process -FilePath $setup -Wait -PassThru -ArgumentList @('modify', '--installPath', "`"$existing`"", '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended', '--passive', '--norestart')
            if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { Write-Warning "The Visual Studio Installer returned exit code $($p.ExitCode); trying the Build Tools installer instead." }
        }
    }
    if (-not (Get-VsPath)) {
        # Still nothing usable. Use Microsoft's own bootstrapper rather than winget: it installs a fresh copy, and it also
        # repairs/modifies one that winget reports as "already installed" but whose registration is incomplete.
        Write-Host 'Downloading the Visual Studio Build Tools installer and adding the C++ workload (this takes a while)...'
        $boot = Join-Path ([IO.Path]::GetTempPath()) 'vs_BuildTools.exe'
        Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $boot -UseBasicParsing
        $p = Start-Process -FilePath $boot -Wait -PassThru -ArgumentList @('--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended', '--passive', '--norestart', '--wait')
        if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { throw "The Visual Studio Build Tools installer failed (exit code $($p.ExitCode))." }
    }
    $vs = Get-VsPath
    if (-not $vs) {
        $vswhereExe = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        Write-Host '--- what vswhere can see (send this if the build still stops here) ---'
        if (Test-Path $vswhereExe) { & $vswhereExe -all -prerelease -products '*' -format text } else { Write-Host 'vswhere.exe is missing: the Visual Studio Installer is not installed.' }
        Write-Host '------------------------------------------------------------------------'
    }
    if (-not $vs) { throw 'The C++ workload is still not visible. A restart may be needed; otherwise open Visual Studio Installer, choose Modify, and tick "Desktop development with C++". Then run this script again.' }
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
function Test-PythonWithPip([string]$Exe, [string[]]$Prefix) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'      # a native program's stderr must not become a terminating error here
    try { & $Exe @($Prefix + @('-m', 'pip', '--version')) *> $null; return ($LASTEXITCODE -eq 0) }
    catch { return $false }
    finally { $ErrorActionPreference = $saved }
}
function Get-PythonExe {
    # The py launcher first (it only sees real python.org installs), then python on PATH. A Python without pip
    # (for example the one that ships with MSYS2) is skipped.
    foreach ($c in 'py', 'python', 'python3') {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $cmd -or $cmd.Source -like '*WindowsApps*') { continue }   # the Microsoft Store stub is not a real Python
        $prefix = @()
        if ($c -eq 'py') { $prefix = @('-3') }
        if (Test-PythonWithPip $c $prefix) { return $c }
        Write-Host "skipping $($cmd.Source): it has no pip"
    }
    return $null
}
function Invoke-Python([string[]]$PyArgs, [switch]$Capture) {
    $exe = Get-PythonExe
    if (-not $exe) { throw 'Python 3 was not found.' }
    $all = @()
    if ($exe -eq 'py') { $all += '-3' }   # the py launcher needs to be told to use Python 3
    $all += $PyArgs
    if ($Capture) { return (& $exe @all) }
    Invoke-Checked $exe $all
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
    if (-not (Get-PythonExe)) {
        Write-Host 'No Python with pip found; installing Python 3.12 with winget...'
        Install-WithWinget -Id 'Python.Python.3.12'
        if (-not (Get-PythonExe)) { throw 'Python was installed but is not usable yet. Close this window, open a new PowerShell as administrator and run the script again.' }
    }
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
    # Visual Studio sets VCPKG_ROOT to its own bundled vcpkg. That copy is not a git checkout and refuses to build ports
    # unless the project pins a vcpkg baseline, so it is skipped. A normal clone (our own, or the one on GitHub's runners) works.
    foreach ($r in @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT, (Join-Path $DepsDir 'vcpkg'))) {
        if (-not $r) { continue }
        if ($r -like '*Microsoft Visual Studio*') { continue }
        if (Test-Path (Join-Path $r 'scripts\buildsystems\vcpkg.cmake')) { return $r }
    }
    return $null
}

$Vcpkg = $null
if ($OpenSslRoot) {
    Step 'OpenSSL (prebuilt)'
    if (-not (Test-Path (Join-Path $OpenSslRoot 'include\openssl\evp.h'))) { throw "-OpenSslRoot '$OpenSslRoot' has no include\openssl\evp.h" }
    Write-Host "using OpenSSL at $OpenSslRoot"
} else {
    Step 'vcpkg (builds OpenSSL 3 from source)'
    $Vcpkg = Get-VcpkgRoot
    if (-not $Vcpkg) {
        if (-not $InstallPrereqs) { throw 'vcpkg was not found. Set VCPKG_ROOT to an existing vcpkg, or re-run with -InstallPrereqs to clone one.' }
        if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Install-WithWinget -Id 'Git.Git' }
        $Vcpkg = Join-Path $DepsDir 'vcpkg'
        Invoke-Checked 'git' @('clone', '--depth', '1', 'https://github.com/microsoft/vcpkg', $Vcpkg)
        Invoke-Checked (Join-Path $Vcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    $env:VCPKG_ROOT = $Vcpkg
    $env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $DepsDir 'vcpkg-archives'
    New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
    Write-Host "using vcpkg at $Vcpkg"
}

# ---- configure, build, test -----------------------------------------------------------------------------------
$cache = Join-Path $Build 'CMakeCache.txt'
if ($Vcpkg -and (Test-Path $cache)) {
    $toolchainFile = Join-Path $Vcpkg 'scripts\buildsystems\vcpkg.cmake'
    $wanted = ($toolchainFile -replace '\\', '/')
    if (-not ((Get-Content $cache -Raw) -like "*$wanted*")) {
        Write-Host 'The build folder was configured with a different toolchain; starting it afresh.'
        $Clean = $true
    }
}
if ($OpenSslRoot -and (Test-Path $cache)) {
    # A build folder that was configured for vcpkg remembers the vcpkg toolchain and would build OpenSSL from source again.
    if ((Get-Content $cache -Raw) -match 'vcpkg') {
        Write-Host 'The build folder was configured for vcpkg; starting it afresh for the prebuilt OpenSSL.'
        $Clean = $true
    }
}
if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

function Show-ConfigureDiagnostics {
    # "unable to find Ninja" / "CMAKE_CXX_COMPILER not set" are usually only the LAST symptom. Show what actually failed.
    Write-Host ''
    Write-Host '--- diagnostics (the real cause is usually here) ---' -ForegroundColor Yellow
    $vlog = Join-Path $Build 'vcpkg-manifest-install.log'
    if (Test-Path $vlog) {
        Write-Host "vcpkg log ($vlog), error lines and the last lines:"
        Get-Content $vlog | Select-String -Pattern 'error|Error|failed|FAILED' | Select-Object -Last 15 | ForEach-Object { Write-Host "  $($_.Line)" }
        Write-Host '  ...'
        Get-Content $vlog -Tail 25 | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host 'no vcpkg log was written (vcpkg did not run, or this build does not use it)'
    }
    if ($Vcpkg -and (Test-Path (Join-Path $Vcpkg 'buildtrees'))) {
        $logs = Get-ChildItem -Path (Join-Path $Vcpkg 'buildtrees') -Recurse -Filter '*-err.log' -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending | Select-Object -First 2
        foreach ($l in $logs) {
            Write-Host ''
            Write-Host "vcpkg package log $($l.FullName) (last 40 lines):" -ForegroundColor Yellow
            Get-Content $l.FullName -Tail 40 | ForEach-Object { Write-Host "  $_" }
        }
    }
    foreach ($tool in 'cmake', 'ninja', 'cl') {
        $c = Get-Command $tool -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($c) { Write-Host "$tool : $($c.Source)" } else { Write-Host "$tool : NOT FOUND on PATH" }
    }
    $err = Join-Path $Build 'CMakeFiles\CMakeError.log'
    if (Test-Path $err) { Write-Host 'CMakeError.log (last lines):'; Get-Content $err -Tail 15 | ForEach-Object { Write-Host "  $_" } }
    Write-Host 'If the vcpkg log mentions a build failure, send me those lines. Re-running with -Clean also clears stale state.'
    Write-Host '-----------------------------------------------------' -ForegroundColor Yellow
}

function Invoke-Configure([string[]]$Extra) {
    $cfg = @('-G', 'Ninja', '-S', $Repo, '-B', $Build, "-DCMAKE_BUILD_TYPE=$Config", "-DCMAKE_PREFIX_PATH=$Qt", '-DTC_BUILD_APP=ON')
    if ($OpenSslRoot) {
        $lib = Get-ChildItem -Path (Join-Path $OpenSslRoot 'lib') -Recurse -Filter 'libcrypto*.lib' -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notmatch 'static' -and $_.Name -notmatch 'libcryptod' } |
               Sort-Object { if ($_.FullName -match '\\MD\\') { 0 } else { 1 } } | Select-Object -First 1
        $cfg += @("-DOPENSSL_ROOT_DIR=$OpenSslRoot", "-DOPENSSL_INCLUDE_DIR=$(Join-Path $OpenSslRoot 'include')")
        if ($lib) { $cfg += @("-DOPENSSL_CRYPTO_LIBRARY=$($lib.FullName)"); Write-Host "using OpenSSL library $($lib.FullName)" }
        else { Write-Warning "no libcrypto*.lib found under $OpenSslRoot\lib; is this the 'Dev' build of OpenSSL (with libraries)?" }
    } else {
        $cfg += @("-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $Vcpkg 'scripts\buildsystems\vcpkg.cmake')",
                  "-DVCPKG_TARGET_TRIPLET=$Triplet", "-DVCPKG_INSTALLED_DIR=$(Join-Path $DepsDir 'vcpkg_installed')")
    }
    try { Invoke-Checked 'cmake' ($cfg + $Extra) }
    catch { Show-ConfigureDiagnostics; throw }
}

# CMake's zip extractor sometimes fails on Strawberry Perl ("Can't restore time"), which the OpenSSL build needs. Unpack the
# zip vcpkg already downloaded with .NET instead, where vcpkg looks for it, so that a retry finds Perl and skips extraction.
function Repair-VcpkgPerl {
    $dl = Join-Path $Vcpkg 'downloads'
    $zip = Get-ChildItem -Path $dl -Filter 'strawberry-perl-*-portable.zip' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $zip) { return $false }
    if ($zip.Name -notmatch 'strawberry-perl-([0-9.]+)-64bit-portable\.zip') { return $false }
    $dest = Join-Path $dl ('tools\perl\' + $Matches[1])
    if (Test-Path (Join-Path $dest 'perl\bin\perl.exe')) { return $false }   # Perl is already there: something else failed
    Write-Host "CMake could not unpack $($zip.Name); unpacking it with .NET instead..."
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip.FullName, $dest)
    return (Test-Path (Join-Path $dest 'perl\bin\perl.exe'))
}

if ($OpenSslRoot) { Step 'Configuring' } else { Step 'Configuring (the first run builds OpenSSL, which takes several minutes)' }
try {
    Invoke-Configure @('-DTC_DEV_TOOLS=OFF')
} catch {
    if ($OpenSslRoot -or -not (Repair-VcpkgPerl)) { throw }
    Write-Host 'Perl is in place; configuring again...'
    Invoke-Configure @('-DTC_DEV_TOOLS=OFF')
}
Step 'Building'
Invoke-Checked 'cmake' @('--build', $Build)

$savedPath = $env:Path
$env:Path = (Join-Path $Qt 'bin') + ';' + $env:Path     # so the test programs find Qt's DLLs
if ($OpenSslRoot) { $env:Path = (Join-Path $OpenSslRoot 'bin') + ';' + $env:Path }

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
if ($OpenSslRoot) { Copy-Item (Join-Path $OpenSslRoot 'bin\libcrypto*.dll') $Stage }   # a prebuilt OpenSSL is a DLL
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
    "vcpkg:     $Vcpkg  (triplet $Triplet)  openssl-root: $OpenSslRoot",
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
