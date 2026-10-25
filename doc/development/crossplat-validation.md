# Crossplat updater validation — 2026-10-25

These checks use `/home/kyllan/crossplat/xp`, its dedicated Windows VM, physical
Apple Silicon Mac, and persistent Linux containers. Results below distinguish
component tests, real platform installers, and complete application GUI checks.
No public fork releases were created or installed over a personal application.

## Results

| Environment | Updater components | Complete application |
| --- | --- | --- |
| Ubuntu 22.04 build container | 42 portable + 144 Linux assertions | Final full application/AppImage build and main-window startup pass; screenshot inspected. |
| Ubuntu 24.04 | 42 portable + 144 Linux assertions | Main window; real GUI HTTPS update checks, cancellation, replacement, backup, deferred restart, save cancellation, successful project save and actual relaunch with the custom profile preserved. |
| Ubuntu 26.04 | 42 + 144 assertions | Main window passes after the production private-font-cache fix; screenshot inspected. |
| Fedora 44 | 42 + 144 assertions | Main window rendered under Xvfb, screenshot inspected. |
| Mint 22.2 package environment | 42 + 144 assertions | Main window rendered under Xvfb, screenshot inspected. |
| Debian 13 | 42 + 144 assertions | Main window rendered after installing/generating `en_US.UTF-8`; screenshot inspected. |
| openSUSE Tumbleweed | 42 + 144 assertions | Main window rendered under Xvfb, screenshot inspected. |
| Arch rolling | 42 + 144 assertions | Main window rendered under Xvfb, screenshot inspected. |
| Alpine 3.24 / musl | 42 + 144 assertions | Official glibc AppImage is not a supported native package here. |
| Windows 11 x64 | Native MSVC and MinGW builds: 42 assertions each; 8 PowerShell 5.1 helper scenarios; real NSIS install and cancellation | Full Visual Studio 2022 application build in progress. |
| macOS 27.0.1 / Apple Silicon | Native AppleClang: 42 assertions; 32 native DMG/helper assertions | Full arm64 application build and visible native window pass with a disposable profile. |

Linux containers share the host kernel. GUI checks use Xvfb and software OpenGL;
they do not validate a complete distro desktop, Wayland, physical GPUs, printer
connections, or account login. The Mint image contains the Mint package repository
on an Ubuntu base; it is not a full Cinnamon installation. Flatpak keeps the
upstream package-manager update behavior and was not installed for these tests.

The Mac full build used deployment target 14.0 because the available Xcode 21
libc++ headers reject the upstream 10.15 target under a dependency's `-Werror`.
It also links a Homebrew Imath library built for macOS 26. This establishes a
native build/startup on the test Mac, not standalone distribution or compatibility
with older macOS versions. The existing `/Applications/BambuStudio.app` was left
running and unchanged.

## Native installer and transport checks

`tests/application_updater/macos_native_tests.py` builds tiny signed universal
app fixtures, packages real DMGs, and invokes production preparation/helper code.
It checks read-only mounting, native codesign/lipo/ditto/stat, detachment,
replacement, backup rotation, changed-install and symlink rejection, rollback,
invalid signatures, incompatible CPU architecture, read-only installations, and
actual LaunchServices relaunch with the exact custom profile arguments. Only failure-alert dialogs are suppressed; rollback
injects a failing second rename. No personal installed app is modified.

`tests/application_updater/windows_native_tests.ps1` uses the production helper
and NSIS packaging script with a tiny Windows program. It checks parent-process
waiting, fixed installation directory, real installer pages, version replacement,
package cleanup and updated-program relaunch. `-CancelInstaller` checks real
installer cancellation, preserved files, the failure dialog, cleanup and no
relaunch. The launched fixture records and verifies the actual custom profile
arguments, including a trailing backslash. Paths include spaces, brackets,
apostrophes, `$`, `é` and `漢`. The VM's
SSH account is already elevated: these tests do **not** establish UAC behavior on
a normal unelevated desktop.

Using the application’s exact MSVC libcurl 7.75/OpenSSL 3.1.2 dependencies, the
Windows driver passed the 42 assertions and a real HTTPS download. The MinGW
build was tested separately.

The production download driver also downloaded and SHA-256-verified a real
229,153,272-byte public GitHub payload on both native macOS and Windows. The
payload was used only as inert transport-test bytes; it was never executed.
Native TLS/download verification and platform installation are separate tests.

## Ubuntu GUI scenarios and findings

The complete fork downloaded from a loopback HTTPS fixture inside the disposable
Ubuntu 24 container. A temporary container-only test CA and hosts entries routed
`api.github.com` and `github.com` to that fixture. No physical host or Mac trust
settings were changed. Metadata described synthetic version `02.09.00.01` and
served real fork AppImage payloads (257,272,312 bytes initially and 257,255,928 bytes after the fixes). Replacing the file
with identical app bytes tests the transaction and restart, not a genuine version
migration.

Observed scenarios: SHA-256 rejection of corrupted bytes; cancellation of a
visibly active throttled download; HTTP 503 and 403 failure messages; successful
replacement with a changed inode and the original inode retained as `.previous`;
restart deferral; rechecking a prepared update while the fixture is offline;
an unsaved cube project's close prompt; cancellation preserving the live app;
and actual relaunch of the replaced AppImage. The final run saved the 20 mm cube
to its existing 3MF path, verified the archive and mesh dimensions, and confirmed
the restarted process received the exact custom `--datadir`. Saving used the
existing project path, so that run did not open a separate file-save chooser.

Relaunch exposed loss of a custom `--datadir` argument, causing a fresh profile
and setup wizard. Linux restart now passes the current data directory as a literal
argument. Its regression test verifies paths containing spaces, apostrophes,
Unicode, and shell substitution text. The rebuilt AppImage passes the actual
custom-profile restart regression. Windows and macOS helpers now preserve the
same current data directory; native fixtures verify their actual relaunch argv.
A second issue found on the physical Mac
was DMG packaging's multi-architecture `lipo -verify_arch` invocation; packaging
now verifies each architecture separately.

Ubuntu 26's initial reproducible SIGSEGV was captured both by systemd-coredump and gdb.
The stack enters `libpangoft2-1.0.so.0.5700.0`, then GTK text sizing,
`wxStaticText`, `Label`, and `AmsHumidityTipPopup::AmsHumidityTipPopup` during main
window construction. Other threads were in normal font, graphics and webview
work. The official upstream 02.08.02.61 AppImage reaches its main window in the same
container (with a configuration-update prompt). Pango 1.57's source and the
crash instruction point to a missing font family in its cache. A temporary
interposer calling `pango_fc_font_map_config_changed` after wx's private-font
registration makes the fork's unchanged AppImage open successfully. The production
fix refreshes the default font map after all bundled fonts are registered; its
rebuilt AppImage opens its main window successfully without the diagnostic library. No updater frame
was on the captured crashing stack.

## Reproduce

Build component suites using the commands in [application-updater.md](application-updater.md).
On Linux, install GTK3, WebKit2GTK 4.1, Mesa software OpenGL, dbus, Xvfb, xdotool,
xwininfo, Python GObject/Gdk bindings and `en_US.UTF-8`. Run in a disposable target:

```sh
xp run ubuntu24 -p bambu-updater --gui -- \
  'dbus-run-session -- python3 tests/application_updater/linux_gui_smoke.py ./BambuStudio_linux-x86_64.AppImage results/fresh-smoke'
```

The results directory must not already exist. The script saves logs, window
metadata and screenshots, and only terminates its own application process group.
Inspect the screenshot before counting a main-window startup as a pass.

For macOS:

```sh
python3 tests/application_updater/macos_native_tests.py build/portable/updater_native_driver
python3 tests/application_updater/macos_gui_smoke.py path/to/BambuStudio.app results/fresh-smoke
```

For Windows startup, run the following in the logged-in interactive test desktop
(the script creates a fresh profile and closes only its own process):

```powershell
& .\tests\application_updater\windows_gui_smoke.ps1 -Application path\to\bambu-studio.exe -Results results\fresh-smoke
```

For helper tests, use Windows PowerShell 5.1 in the disposable elevated test VM:

```powershell
& .\tests\application_updater\windows_helper_tests.ps1
& .\tests\application_updater\windows_native_tests.ps1
& .\tests\application_updater\windows_native_tests.ps1 -CancelInstaller
```

`release_fixture.py` preserves the Linux GUI fixture server. It accepts payload,
certificate/key and mode-file paths, binds only loopback, and supports `normal`,
`slow`, `corrupt`, `offline`, and `rate-limit` modes. Configure certificate trust
and name resolution **only inside a disposable container**, remove both after
use, and run the application with an isolated data directory. Use the fork's exact
configured repository and asset name; no remote release needs to be published.

## Cleanup

The Ubuntu 24 fixture server and GUI processes were stopped. Its marked hosts
entry, test certificate and private key were removed; the CA bundle was rebuilt
with `update-ca-certificates --fresh` and checked for remaining fixture entries.
The task's Ubuntu 22 build container was stopped without deleting artifacts.
Cleanup logs: `20261025-134023-ubuntu24-bambu-updater-1631916.log` and
`20261025-134138-ubuntu24-1633595.log`.

## Local evidence

Raw crossplat logs are retained under `/mnt/gamesDrive/crossplat/logs`:

The latest Linux component runs use `xp run <target> -p bambu-updater --timeout 180 -- 'bash run-tests.sh'`
(invoked through `xp matrix`), and each passes 42 portable + 144 Linux assertions:

| Target | Exit | Wall time | Log |
| --- | --- | --- | --- |
| Ubuntu 24.04 | 0 | 13 s | `20261025-141559-matrix-ubuntu24.log` |
| Alpine 3.24 | 0 | 19 s | `20261025-141559-matrix-alpine.log` |
| Ubuntu 26.04 | 0 | 28 s | `20261025-142025-matrix-ubuntu.log` |
| Mint package environment | 0 | 29 s | `20261025-142025-matrix-mint.log` |
| Debian 13 | 0 | 29 s | `20261025-142025-matrix-debian.log` |
| Fedora 44 | 0 | 28 s | `20261025-142025-matrix-fedora.log` |
| openSUSE Tumbleweed | 0 | 30 s | `20261025-142025-matrix-opensuse.log` |
| Arch rolling | 0 | 31 s | `20261025-142025-matrix-arch.log` |

The Mac native command `cmake --build build/portable --parallel 2 && build/portable/application_updater_tests && python3 tests/application_updater/macos_native_tests.py build/portable/updater_native_driver`
returns 0 in 43 s (`20261025-141148-mac-bambu-updater-1656682.log`). Windows
PowerShell 5.1 helper tests followed by real NSIS success and cancellation return
0 in 28 s (`20261025-141203-win11-bambu-updater-1656773.log`); commands are shown
in the reproduction section and each log header. Native MSVC recompilation and
the portable executable return 0 in 8 s (`20261025-141921-win11-bambu-updater-1669671.log`).

Additional application and integration evidence:

- Ubuntu 22 final GUI: `build/gui-production-font-fix-ready/{startup.log,startup.png}`; runner log `/tmp/bambu-ubuntu22-gui-final.log`.
- Final Linux AppImage: 257,255,928 bytes, SHA-256 `dd70feda85e307121cae674f3f405c979412f2d4a6976b507e54738a361d010b`.
- Ubuntu 26 production fix: `20261025-133007-ubuntu-bambu-updater-1616193.log`.
- Final Fedora/Mint/Debian/openSUSE/Arch GUI: `20261025-133101-matrix-*.log`.
- Ubuntu 26 upstream baseline: `20261025-131338-ubuntu-bambu-upstream-baseline-1599958.log`.
- Ubuntu 26 font-map probe: `20261025-131834-ubuntu-bambu-font-probe-1605543.log`.
- Final Linux components: `20261025-124453-matrix-*.log` and `20261025-124537-matrix-*.log`.
- Latest helper simulation regression, all eight Linux targets: `20261025-141559-matrix-*.log` and `20261025-142025-matrix-*.log` (42 portable + 144 Linux assertions each).
- Mac native suite: `20261025-124638-mac-bambu-updater-1577114.log`.
- Mac final-source incremental build/startup: `20261025-135638-mac-bambu-updater-full-1645397.log`.
- Mac custom-profile native suite (42 portable + 32 native): `20261025-141148-mac-bambu-updater-1656682.log`.
- Mac custom-profile source rebuild/startup: `20261025-141316-mac-bambu-updater-full-1658108.log`.
- Mac full build: `20261025-095108-mac-bambu-updater-full-1434967.log`.
- Mac visible-window startup: `20261025-125742-mac-bambu-updater-full-1586291.log`.
- Windows MSVC build/transport: `20261025-131128-win11-bambu-updater-1598065.log`.
- Windows MSVC helper-source recompile and 42 assertions: `20261025-141921-win11-bambu-updater-1669671.log` (remote source timestamps refreshed to avoid host/guest clock skew).
- Windows fresh MinGW build/transport: `20261025-125535-win11-bambu-updater-1584453.log`.
- Windows real NSIS success: `20261025-124625-win11-bambu-updater-1576995.log`.
- Windows real NSIS cancellation: `20261025-124433-win11-bambu-updater-1573354.log`.
- Windows custom-profile helper/real NSIS success and cancellation: `20261025-141203-win11-bambu-updater-1656773.log`.
- Ubuntu final save/geometry/profile restart: `20261025-133844-ubuntu24-bambu-updater-1630444.log`.
- Ubuntu final replacement/backup: `20261025-133234-ubuntu24-bambu-updater-1623365.log`.
- Ubuntu GUI replacement/backup: `20261025-123418-ubuntu24-bambu-updater-1560082.log`.
- Ubuntu save cancellation: `20261025-123751-ubuntu24-bambu-updater-1565583.log`.

Linux screenshots and application logs are under each target's
`work/<target>/bambu-updater/results`; native Mac/Windows evidence is in their
corresponding remote workspaces. Test certificates, keys, large binaries,
profiles and raw cores are not committed.

Remaining native coverage includes Windows ARM64, actual Intel Mac execution,
normal Windows UAC acceptance/cancellation, macOS Developer ID notarization and
Gatekeeper distribution, old macOS versions, and interactive native Mac/Windows
save/update dialogs. No public installer is ready for distribution solely on the
basis of these component and fixture results.
