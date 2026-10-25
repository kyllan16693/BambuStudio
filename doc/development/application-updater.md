# In-app updates for this fork

The fork downloads and verifies updates inside Bambu Studio on Windows, macOS,
and Linux. The first fork build must be installed manually once. Subsequent
updates come from public GitHub releases in `kyllan16693/BambuStudio`; official
Bambu binaries would remove the feature. No fork releases are published by the
build or test scripts.

The existing startup check and **Check for updates** open the release dialog
with **Install update**. Downloads stream to disk on a worker thread with
progress and cancellation. Stable releases are the default; the existing beta
preference includes prereleases. Before use, every package must match the exact
configured asset name and repository URL, its expected byte count, and the
SHA-256 digest provided by GitHub over HTTPS. Failed or cancelled downloads are
deleted. This digest relies on the repository and GitHub account; it is not an
independently signed release manifest.

## Platform behavior

| Platform | Exact release asset | Installation |
| --- | --- | --- |
| Linux x86-64 | `BambuStudio_linux-x86_64.AppImage` | Atomic replacement of a writable, user-owned type-2 AppImage; `.previous` backup retained. |
| Windows x86-64 | `BambuStudio_windows-x86_64.exe` | Verified NSIS installer runs after the app exits, with the current installation directory fixed for the update. UAC and installer UI remain visible. |
| Windows ARM64 / ARM64EC | `BambuStudio_windows-arm64.exe` | Same installer flow; publish the ARM64EC build when the x64 network plugin is required. |
| macOS Intel / Apple silicon | `BambuStudio_macos-universal.dmg` | Mount read-only, verify the app signature and local CPU architecture, copy beside the current app, then replace after exit. Retain `.previous`; restore it if replacement fails. |

**Install now** uses the normal close path, including project save prompts.
Cancelling a save keeps the app open. Deferred web-project saves keep the update
request until the save completes. Rechecking with an update ready offers the
installation/restart again without downloading it twice. Save your project
before restarting; the updater does not reopen unsaved work automatically.

On Linux the replacement is installed immediately and **Restart now** is
optional. Other platforms install only after choosing to close and install.
Choosing later keeps the verified download for the current session; a normal
exit without installation discards it. Windows PowerShell 5.1 runs the local
helper without loading profiles. The Windows installer may request administrator
credentials. Cancelling that installer leaves the app closed; launch it again
normally. Installer errors are shown by the helper.

macOS requires a writable app and parent directory, owned by the current user;
move an app from a read-only disk image or protected system directory into a
writable Applications folder first. The helper detects a renamed/replaced
installation and serializes replacement with a lock directory. It rotates an
existing regular backup, rejects symlink paths, and preserves the immediate
previous version. A failed swap keeps the staged download for diagnosis. A
crash or power loss during the two directory renames can require manually
renaming `.previous` back. Full macOS replacement is not atomic across both
renames.

Linux Flatpak and non-AppImage installations retain the upstream behavior.
Linux ARM and Windows 32-bit do not enable this updater by default.

## Build and package releases

Increment the four-component `SLIC3R_VERSION` in `version.inc` for every release.
Use the matching tag, e.g. `v02.08.04.58`, and upload the exact assets above.
GitHub's asset `digest` must be `sha256:<64 lowercase hex digits>`; absent digests
fail closed. Drafts and tags without four numeric version components are ignored.
The updater compares numeric versions across the latest 100 releases.

Build with the upstream platform dependencies and scripts. For Linux:

```sh
sudo apt-get install -y libgtk-3-dev
sudo ./BuildLinux.sh -ur
./BuildLinux.sh -dsir
cp build/BambuStudio_ubu64.AppImage build/BambuStudio_linux-x86_64.AppImage
```

For Windows, build the appropriate x64 or ARM64EC CMake configuration and its
`install` target, then package the resulting complete `install-dir` tree with
NSIS 3. Use absolute paths (PowerShell example):

```powershell
& 'C:\Program Files (x86)\NSIS\makensis.exe' `
  "/DAPPDIR=$PWD\install-dir" "/DOUTFILE=$PWD\BambuStudio_windows-x86_64.exe" `
  packaging/updates/windows.nsi
```

Sign the resulting installer with your release Authenticode certificate if
available. Without one, Windows can show an unknown-publisher warning. The
installer installs the entire app tree and registers a separate fork uninstaller;
it does not modify slicer preferences stored in the user profile. It uses the
current directory during updates (`/UPDATE /D=...`); initial installation offers
a directory picker. Build/package **one** ARM64 variant per release; ARM64 and
ARM64EC share the ARM64 asset channel and must not contain incompatible binaries.

For macOS, `./BuildMac.sh -s -x -a universal -t 10.15 -1` builds a universal bundle
once the upstream dependencies exist. Sign the finished bundle, including its
nested executables/libraries, with your release identity, then run:

```sh
packaging/updates/macos-dmg.sh build/BambuStudio/BambuStudio.app "$PWD/BambuStudio_macos-universal.dmg"
```

The script requires a valid signature and both architectures. For local testing,
a correctly applied ad-hoc signature can pass verification; distribution should
use Developer ID signing and notarization/stapling to avoid Gatekeeper friction.
The updater verifies signature integrity; it does not pin a developer team ID.
It trusts the bytes selected by the configured GitHub repository.

Use these CMake cache entries for another fork or asset:

```
-DBAMBUSTUDIO_UPDATE_REPOSITORY=owner/repository
-DBAMBUSTUDIO_UPDATE_ASSET=<exact-platform-asset>
```

An empty repository disables the feature. Do not point a fork build at releases
that lack the updater.

## Tests and CI

The portable tests build on all three platforms with CMake 3.16+, C++17, libcurl,
and OpenSSL. Windows can use the vcpkg toolchain for those dependencies:

```sh
cmake -S tests/application_updater -B build/application-updater-tests
cmake --build build/application-updater-tests --config Debug
ctest --test-dir build/application-updater-tests -C Debug --output-on-failure
```

`tests/application_updater/windows_helper_tests.ps1` parses the actual helper
source and runs four scenarios with mocked process/installer commands and a
headless alert: normal install, already-exited parent, cancelled installer, and
parent timeout. It also checks literal paths, the fixed installation directory,
cleanup, and restart ordering. It runs with PowerShell 7 on Linux or Windows;
Windows PowerShell 5.1 has also been exercised in the native VM; real UAC on an unelevated desktop still requires verification.

The Linux suite additionally tests atomic replacement, inode preservation,
backup rotation, concurrent updates, unsafe paths, restart waiting/environment,
and the macOS helper's actual directory transaction with substituted macOS
launch/notification tools, including rollback and changed-installation detection:

```sh
cmake -S tests/appimage_updater -B build/updater-tests
cmake --build build/updater-tests -j2
ctest --test-dir build/updater-tests --output-on-failure
```

`doc/development/application-updater-workflow.yml` is an inert cross-platform
CI template. Copy it to `.github/workflows/application-updater.yml` using GitHub
credentials with workflow permission to activate it. The existing Linux full
AppImage build template remains at `appimage-updater-workflow.yml`. Neither
publishes releases. This account's current GitHub credentials can push source
but cannot change active workflows.

Before distributing, test the complete GUI update on each native OS: successful
installation and launch, download cancellation, offline/rate-limited checks,
permission failures, unsaved projects and cancelled/deferred saves. On Windows,
test UAC acceptance/cancellation and Unicode/spaced installation paths. On macOS,
test mounted-image rejection, Intel/Apple silicon, signature failure, backup
rotation, and rollback. Portable tests and Linux shell transaction tests do not
replace native GUI/installer testing.

## Current validation result

Crossplat testing on 2026-10-25 passed 42 portable and 144 Linux assertions on
each of Ubuntu 24/26, Fedora, Mint's package environment, Debian, openSUSE, Arch,
and Alpine. Native Windows and Mac builds of the portable suite passed all 42
assertions. The native Mac DMG/helper suite passed 31 assertions; real Windows
NSIS installation/relaunch and cancellation passed. Native HTTPS downloads on
both Windows and Mac verified a 229,153,272-byte public release payload.

The full Mac application built and opened a visible native window with an
isolated profile. Final Linux GUI tests reached the main window on Ubuntu 22/24/26, Fedora,
Mint, Debian, openSUSE, and Arch. Ubuntu 26 testing found and fixed a stale
private-font-family cache in Pango initialization. Ubuntu
24's actual GUI exercised HTTPS failure, corruption rejection, cancellation,
replacement, backup, save cancellation, deferral, recheck, and relaunch. That
relaunch found loss of a custom data directory, now fixed and verified through
a complete GUI save/update/restart with the original profile and a valid saved 3MF.

See [crossplat-validation.md](crossplat-validation.md) for exact evidence,
reproduction commands, environment caveats, full-build status, and outstanding
native GUI/distribution checks. No public fork release has been published.
