# Linux packaging

The native Linux script builds and runs CTest before packaging. It deploys an
x86_64 AppDir with Qt libraries and plugins, LibArchive, C++ runtimes, and their
transitive shared libraries. Every bundled executable and plugin is checked for
unresolved or accidentally host-resolved dependencies. The dependency manifest
and Debian/Ubuntu SDK copyright notices are saved under
`usr/share/prismatic-launcher/` in the AppDir. No game or mod binary is executed.

Install CMake 3.28+, Ninja, a C++20 compiler, Qt 6 development packages (including
`qmake` from the same SDK), LibArchive development files, Python 3.9+, `readelf`,
`ldd`, `sha256sum`, ripgrep, and `desktop-file-validate`. On Ubuntu 24.04:

```sh
sudo apt-get install cmake ninja-build g++ qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools libarchive-dev python3 desktop-file-utils ripgrep
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s packaging/linux -p 'test_*.py' -v
./scripts/package-linux.sh --appdir-only
./build/artifacts/linux/PrismaticLauncher.AppDir/AppRun
```

The script takes `--build-dir`, `--output-dir` and optional `--config`. An existing build must have
`BUILD_TESTING=ON`; it is built and tested again. Existing packaging output is
never overwritten. Output defaults to `build/artifacts/linux/`. CMake's
`package-linux` target invokes the script with its current build directory.
Generated `qt-sdk-<configuration>.json` metadata identifies the exact configured
Qt SDK, avoiding accidentally deploying plugins from another SDK on `PATH`.
Different libraries with the same SONAME are rejected instead of substituted.

The supplied tools in `tools/appimage/` are now pinned by commit/build and SHA-256
in [their manifest](../tools/appimage/manifest.json). The verified tool is
appimagetool commit `854e19e`, build 313; the type-2 runtime is commit `8f39b89`.
The [local tool guide](../tools/appimage/README.md) contains the exact pinned
build command. For other explicitly verified tool versions, supply local paths
and verified checksums:

```sh
APPIMAGETOOL=/absolute/path/to/appimagetool \
APPIMAGETOOL_SHA256='<verified 64-digit SHA-256>' \
APPIMAGE_RUNTIME=/absolute/path/to/runtime-x86_64 \
APPIMAGE_RUNTIME_SHA256='<verified 64-digit SHA-256>' \
./scripts/package-linux.sh --output-dir build/artifacts/linux-appimage
```

The packaging script never downloads tools. It rejects missing tools or checksum
mismatches. An explicit `--runtime-file` prevents deployment tools from implicitly
fetching a runtime. It invokes `appimagetool` with `APPIMAGE_EXTRACT_AND_RUN=1`, allowing
supported tool/runtime versions to work without FUSE. The generated payload is
smoke-tested with `--version` before it is moved into the artifact directory.
The AppImage and its `.sha256` sidecar are named
`PrismaticLauncher-x86_64.AppImage` and
`PrismaticLauncher-x86_64.AppImage.sha256`.
An additional `PrismaticLauncher.AppDir.sha256` manifest records every regular
file in the runnable directory; verify it from the output directory with
`sha256sum --check PrismaticLauncher.AppDir.sha256`.

`SOURCE_DATE_EPOCH` defaults to the last local commit timestamp, and staged file
timestamps are normalized. Repeating a build with the same source, SDK and
deployment tool is the reproducibility boundary; this does not promise identical
binaries across different compilers, Qt builds or deployment-tool versions.

For optional user-menu integration after placing the AppImage in a permanent
location:

```sh
chmod +x /path/to/PrismaticLauncher-x86_64.AppImage
python3 packaging/linux/install-desktop.py /path/to/PrismaticLauncher-x86_64.AppImage
```

This explicit command installs only the desktop entry and original vector icon
under `$XDG_DATA_HOME` (default `~/.local/share`). It preserves spaces and escapes
Desktop Entry field codes in the application path. Re-run it after moving the
AppImage. Remove `applications/org.prismatic.launcher.desktop` and
`icons/hicolor/scalable/apps/org.prismatic.launcher.svg` from that data directory
to remove the menu registration.

AppRun locates libraries and plugins relative to itself and preserves previous
environment values for restoration when the launcher starts an external game.
Persistent launcher data uses Qt standard paths, independently of the read-only
bundle; `--data-dir` remains available for development and explicit overrides.

The workflow `.github/workflows/package-linux.yml` runs on a native Ubuntu 24.04
runner only when manually invoked. It requires an explicitly selected HTTPS tool
URL and independently verified SHA-256 for both tool and runtime, then builds, tests, packages, validates and
uploads workflow artifacts. It never creates a release or publishes packages.

The host supplies glibc and graphics-driver libraries. An AppImage does not make a
binary built against a newer glibc compatible with an older distribution. Ubuntu
24.04 x86_64 is the current build baseline; compatibility with other distributions,
Wayland/X11 graphics stacks, FUSE configurations and actual SMAPI runtimes needs
native validation.

The actual x86_64 AppImage was built with the supplied pinned tools after all
16 CTest entries passed (3.85 seconds). Artifact:
`build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage`, 45,935,096 bytes
(43.8 MiB), SHA-256:
`f86187cd2f1131783326549c4ea46e6a14e760d3accb3dc529ec3dd8cf2f4e4c`.
The `.AppImage.sha256` sidecar and complete AppDir checksum manifest sit beside it.

```sh
(cd build/artifacts/linux-appimage && sha256sum --check PrismaticLauncher-x86_64.AppImage.sha256)
APPIMAGE_EXTRACT_AND_RUN=1 ./build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage
```

The lead and separate QA agent checked actual type-2 headers, embedded runtime,
SquashFS extraction, Qt/LibArchive runtime closure, relocation with spaces,
version/help output, and offscreen GUI startup using isolated Qt standard data
paths outside the image. The runtime matches the pinned input except for the
expected appimagetool-populated `.digest_md5` field. Two lead GUI starts preserved
the same registry; test harnesses terminated their idle process groups on purpose.
No game or mod was launched.

Repacking the identical staged AppDir with the same pinned tools and
`SOURCE_DATE_EPOCH=1792039735` produced the exact same SHA-256. This verifies this
packaging boundary, not reproducibility across different SDKs or source builds.
Normal FUSE-mounted execution and real X11/Wayland displays remain untested;
`APPIMAGE_EXTRACT_AND_RUN=1` works without FUSE. appimagetool reported a nonblocking
missing AppStream metadata warning; desktop entry/icon validation passed.

Evidence: `build/linux-appimage-package.log`,
`build/appimage-lead-validation/{results,reproduction}.json`, and
`build/qa-appimage/{artifact-review,runtime-review,gui-review}.json`.

The layout and relative entry point follow the official
[AppDir specification](https://docs.appimage.org/reference/appdir.html) and
[manual packaging guide](https://docs.appimage.org/packaging-guide/manual.html).
