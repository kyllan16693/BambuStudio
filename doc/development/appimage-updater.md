# In-app updates for the Linux fork

This fork adds **Install update** to the existing version dialog for writable,
user-owned x86-64 type-2 AppImages. Startup and **Check for updates** use public
GitHub releases from `kyllan16693/BambuStudio`. Stable releases are the default;
the existing beta-update preference also permits prereleases. Flatpak and other
installations keep their existing update behavior.

The app downloads in a worker thread with progress and cancellation. It checks
the exact asset name, repository download URL, expected byte count, SHA-256
digest from GitHub release metadata, and AppImage/ELF architecture before
installing. Downloads stream to a private directory beside the image; they are
never buffered as an entire AppImage in memory. HTTPS verification stays on.
Incomplete and invalid files are deleted without changing the installed image.

Installation keeps `<AppImage>.previous` and atomically renames the verified new
image over the installed path. Linux keeps the running process and mount on
the old inode. **Restart now** closes normally, including existing save prompts,
and starts the new image after the old process exits. Choosing **No**, or
cancelling a save prompt, leaves the installed update available for the next
launch. The updater does not reopen unsaved work automatically; save the project
before restarting. A concurrent update or a changed installation is rejected.

## Build and release

The **Linux in-app updater** workflow runs the focused tests on branch pushes.
Run it manually on the feature branch to build the full Ubuntu 22.04 x86-64
AppImage as a downloadable Actions artifact. It does not publish a release.

For subsequent updates, build from this fork, increment the four-component
`SLIC3R_VERSION` in `version.inc`, and create a GitHub release with a matching tag
such as `v02.08.04.58`. Upload the build as:

```
BambuStudio_linux-x86_64.AppImage
```

The asset name is intentionally stable. Publish the artifact built by this fork;
uploading an upstream AppImage would remove the updater on the next launch.
GitHub's asset `digest` must contain `sha256:<64 lowercase hex digits>`. Missing
or invalid digests fail closed. Drafts are ignored; tags without exactly four
numeric version components are ignored. The check examines the latest 100
releases and compares versions numerically, including padded Bambu versions.
The first fork build must be installed manually once.

For another fork, configure these CMake cache entries:

```
-DBAMBUSTUDIO_UPDATE_REPOSITORY=owner/repository
-DBAMBUSTUDIO_UPDATE_ASSET=BambuStudio_linux-x86_64.AppImage
```

An empty `BAMBUSTUDIO_UPDATE_REPOSITORY` disables this update path. Non-x86-64
builds disable it by default. This implementation covers AppImages only; native
package managers, Windows installers, and macOS bundles need their own update
mechanisms. The digest verifies bytes against GitHub's HTTPS metadata; it is not
an independently signed release manifest.

## Validation

The core can be tested without building the slicer or its GUI dependencies:

```sh
cmake -S tests/appimage_updater -B build/updater-tests
cmake --build build/updater-tests -j2
ctest --test-dir build/updater-tests --output-on-failure
```

The suite covers version/channel selection, malformed and untrusted metadata,
size limits, truncated/corrupted/wrong-architecture downloads, cleanup,
concurrent installs, installation changes, symlink handling, repeated backups,
preservation of the old inode during replacement, and restart waiting/environment/
filename handling. The optional
`appimage_download_smoke` target exercises real HTTPS streaming into a disposable
fixture supplied by the caller; it never launches the downloaded image.

Before publishing a distributable release, also exercise the GUI from a built
AppImage: successful update and restart, cancellation during a download,
read-only installation, offline/rate-limited checks, saving an open project,
and cancelling the save prompt. Verify the next launch reports the new version.

To roll back, close the app, rename the current AppImage aside, and rename its
`.previous` backup to the original AppImage path. Keep the backup executable.
