# Building TenChar Desktop on Windows

**Status.** The build scripts were written without access to Windows. They have been run on one Windows PC (Visual Studio
2022 Build Tools, Qt 6.8.3), and every problem found there has been fixed in the scripts, but other machines may find new ones.
If a step fails, the script prints a diagnostics block: send that block, not just the last line.

## 1. Quick start (recommended)

Everything below runs in an **administrator PowerShell** the first time (Windows key → type `PowerShell` → right-click →
*Run as administrator*). Later runs do not need administrator rights.

**Step 1. Get the project** into a folder, for example `D:\GITHUB\TenChar-Desktop`
(unpack the archive with `tar -xzf tenchar-desktop.tar.gz -C D:\GITHUB\TenChar-Desktop`).

**Step 2. Install a prebuilt OpenSSL** (a one-off download of about 300 MB):

```
winget install ShiningLight.OpenSSL.Dev
```

This is a third-party binary (slproweb.com) rather than OpenSSL built from source, which is fine for getting started but not
what you should ship. If `winget` cannot find it, download the 64-bit **Dev** installer from slproweb.com. The app was written
and tested against OpenSSL 3.x; if the installer gives you 4.x and compilation fails with an OpenSSL error, send the first error.

**Step 3. Build, test and package** (one command):

```
cd D:\GITHUB\TenChar-Desktop
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1 -InstallPrereqs -OpenSslRoot "C:\Program Files\OpenSSL-Win64"
```

What it does, in order: finds or installs the Visual Studio C++ tools (adds the *Desktop development with C++* workload if it is
missing), downloads Qt 6.8 into `.deps\Qt` (aqtinstall; Qt itself comes from Qt's servers), configures and compiles, runs all
tests, collects the Qt runtime with `windeployqt`, copies OpenSSL's DLL, then zips and checksums the result.

Typical times: first run 20 to 40 minutes (Visual Studio workload if missing, Qt download, compile); later runs a few minutes.

**Step 4. Run it.**

```
D:\GITHUB\TenChar-Desktop\dist\TenChar-win64\tenchar.exe
```

Run it from that folder, not from `build\`: only `dist\TenChar-win64` has the Qt and OpenSSL DLLs beside it. You also get
`dist\TenChar-win64.zip`, `dist\TenChar-win64.zip.sha256` and `dist\BUILD-INFO.txt`.

## 2. Rebuilding after a code change

Run the Step 3 command again. Qt and OpenSSL are cached, so only your changes are compiled. Add `-SkipTests` for a faster loop.
Add `-Clean` to throw away the `build` folder and start fresh (this fixes most "strange" configure errors).

## 3. Alternative: let vcpkg build OpenSSL from source

Leave out `-OpenSslRoot`:

```
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1 -InstallPrereqs
```

The script clones its own vcpkg into `.deps\vcpkg` and builds OpenSSL 3 from OpenSSL's source, linked statically (so no OpenSSL DLL
to ship). That is the better supply-chain story, but **on the one PC it was tried on, the OpenSSL build failed** at its
`Configure` step ("Error code: 2") for a reason that was never read from the log. If you try it and it fails, send the lines the
script prints under "vcpkg package log", or run:

```
Get-Content D:\GITHUB\TenChar-Desktop\.deps\vcpkg\buildtrees\openssl\prerun-specific-x64-windows-static-md-dbg-err.log -Tail 40
```

## 4. Switches

| Switch | Meaning |
|---|---|
| `-InstallPrereqs` | install what is missing (Visual Studio C++ workload, Git, Python, Qt, vcpkg) |
| `-OpenSslRoot <dir>` | use a prebuilt OpenSSL (folder containing `include\`, `lib\`, `bin\`) instead of building one with vcpkg |
| `-QtDir <dir>` | use a Qt you already have, e.g. `C:\Qt\6.8.3\msvc2022_64`, instead of downloading one |
| `-Clean` | delete the `build` folder first |
| `-SkipTests` | do not run the test suite |
| `-Smoke` | after building, launch the real app headless and screenshot seven screens (into `dist\smoke`) |
| `-Triplet <name>` | vcpkg triplet when vcpkg builds OpenSSL (default `x64-windows-static-md`; `x64-windows` gives a DLL) |
| `-DepsDir <dir>` | where Qt and vcpkg are kept (default `.deps` in the project; a short path such as `C:\tcdeps` avoids long-path trouble) |
| `-OutDir <dir>` | where the package goes (default `dist`) |
| `-SignThumbprint <sha1>` | sign `tenchar.exe` with this certificate (needs `signtool` from the Windows SDK) |

## 5. Cloud build (GitHub Actions)

Push the project to GitHub. `.github/workflows/windows.yml` builds on a clean Windows machine for every push and pull request, runs
the tests and the smoke test, and uploads the zip, checksum and screenshots as artifacts. Pushing a tag such as `v0.1.0` attaches
them to a GitHub release. It uses the vcpkg route (Section 3), so check its first run before trusting it.

## 6. Troubleshooting

| What you see | What it means | What to do |
|---|---|---|
| `Installing the C++ workload needs an administrator shell` | the Visual Studio C++ tools are missing and adding them needs administrator rights | open PowerShell as administrator and run the command again |
| `The C++ workload is still not visible` | the Visual Studio install is incomplete or needs a restart | restart Windows and run again; or open *Visual Studio Installer* → *Modify* → tick **Desktop development with C++**; the script also prints what `vswhere` sees |
| `No module named pip` (from `C:\msys64\...\python.exe`) | the Python on your PATH is MSYS2's, which has no pip | the script now skips it and installs Python 3.12 itself |
| `The property 'Count' cannot be found` | an old copy of `build.ps1` | replace it with the current one |
| `this vcpkg instance requires a manifest with a specified baseline` | CMake picked Visual Studio's bundled vcpkg | the script now ignores that one and uses its own clone |
| `Can't restore time` / `Unable to find PERL` | CMake's zip extractor failed on the Perl that vcpkg downloads | the script unpacks Perl itself and retries once; or use `-OpenSslRoot` (Section 1) |
| `Configure ... Error code: 2` (OpenSSL, under vcpkg) | OpenSSL's own configure step failed; cause not yet known | use `-OpenSslRoot` (Section 1), or send the vcpkg package log |
| `unable to find a build program corresponding to "Ninja"`, `CMAKE_CXX_COMPILER not set` | **only the last symptom** of an earlier failure | read the diagnostics block above it; Ninja itself is fine |
| the build still runs vcpkg although you passed `-OpenSslRoot` | the `build` folder remembers the old vcpkg setup | the current script clears it itself; otherwise add `-Clean` |
| `a DLL was not found` when starting the app (Qt, `libcrypto`, `MSVCP140`) | you started `build\app\tenchar.exe` | start `dist\TenChar-win64\tenchar.exe`; if `MSVCP140`/`VCRUNTIME` is missing, install the Microsoft Visual C++ Redistributable (x64) |
| `module "QtQuick.Shapes" is not installed` | `windeployqt` did not see the QML files | the script passes `--qmldir app\qml`; if you deploy by hand, do the same |
| Windows SmartScreen warns about the program | the exe is not signed | choose *More info* → *Run anyway*, or sign it with `-SignThumbprint` |
| the first-run screen does not scroll, error text sits at the top-left, dialogs do not open | a bug in versions before the 6.8 layout fix | rebuild from the current archive |
| the build is very slow, or files vanish from `.deps` | antivirus scanning the downloads | exclude the project's `.deps` folder in Windows Security |

To see QML errors from the running app (it has no console window):

```
cd D:\GITHUB\TenChar-Desktop\dist\TenChar-win64
cmd /c "set QT_FORCE_STDERR_LOGGING=1&& tenchar.exe 2> %TEMP%\tenchar-log.txt"
Get-Content $env:TEMP\tenchar-log.txt
```

If the picture looks wrong, try software rendering to tell a graphics problem from an app problem:
`$env:QT_QUICK_BACKEND = "software"; .\tenchar.exe`

## 7. Where things live

- Program and its DLLs: `dist\TenChar-win64\`
- Your data (settings, encrypted keys and messages): `%LOCALAPPDATA%\TenChar`. Deleting this folder removes the conversation
  for good; there is no recovery by design.
- Downloaded tools (Qt, vcpkg, caches): `.deps\` in the project. Safe to delete; the next build downloads them again.

## 8. Building by hand (no script)

In an *x64 Native Tools Command Prompt for VS 2022*, with Qt and a prebuilt OpenSSL installed:

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
  -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64" ^
  -DOPENSSL_INCLUDE_DIR="C:\Program Files\OpenSSL-Win64\include" ^
  -DOPENSSL_CRYPTO_LIBRARY="C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD\libcrypto.lib" ^
  -S . -B build
cmake --build build
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Program Files\OpenSSL-Win64\bin;%PATH%
ctest --test-dir build
mkdir dist\TenChar-win64
copy build\app\tenchar.exe dist\TenChar-win64\
copy "C:\Program Files\OpenSSL-Win64\bin\libcrypto*.dll" dist\TenChar-win64\
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --release --qmldir app\qml --no-translations --compiler-runtime dist\TenChar-win64\tenchar.exe
```

`transport_test` and `qml_warnings` are skipped on Windows (they need `sh`, `bash` and the `openssl` tool).
