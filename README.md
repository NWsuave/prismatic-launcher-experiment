# Prismatic Launcher

A local C++20 / Qt 6 Widgets launcher for Stardew Valley with persistent mod profiles, a global installed-mod explorer, runtime themes, and profile-specific dependency checks. Development is confined to this experimental worktree; no remote publication or real game-data changes are part of validation.

## Build and run

Requirements: CMake 3.28+, Ninja or another CMake generator, a C++20 compiler, Qt 6.4+ development packages (Core, Widgets, Concurrent, Network, Test), and the **official libarchive development package**. Qt 6.8 is the preferred SDK; Linux is tested with Qt 6.4.2. On Ubuntu the relevant native packages include `qt6-base-dev`, `libarchive-dev`, `ninja-build`, and `g++`. The vcpkg manifest declares Qt and libarchive for toolchain-based builds.

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
./build/dev/bin/prismatic-launch --data-dir "$PWD/data"
```

If Qt is outside system paths, configure with `-DCMAKE_PREFIX_PATH=/path/to/Qt/SDK`. For an existing vcpkg setup, use its CMake toolchain file. No dependency downloads or system installations are performed by these instructions automatically. Windows produces `bin/prismatic-launch.exe`; macOS produces `bin/prismatic-launch.app`. Use the native packaging scripts below to deploy runtimes. Windows execution remains unverified. Native macOS packaging and non-GUI checks passed in the recorded Apple Silicon session; Linux checks do not replace native platform validation.

The default data directory is Qt `AppLocalDataLocation`, a per-user operating-system application-data directory. The tested Linux default is `~/.local/share/Prismatic/Prismatic Launcher`; Qt selects the corresponding native location on macOS and Windows. Application bundles and executable directories contain no default persistent data. On first startup, an executable-adjacent legacy `data` tree with an instance registry is copied through bounded staging into an absent standard destination; the original is retained. Migration refuses active legacy sessions, links, unsafe files, and existing destination merges. Select a writable location with `--data-dir` or `PRISMATIC_DATA_DIR`. Use `--config /absolute/path/launcher.ini` to read an existing bootstrap configuration; otherwise the INI lives inside the data directory. `--help` lists options. Only one application may open a given data directory at a time.

## Workflows

- **Library:** create, rename, search, select, duplicate, and delete instances. Names, game paths, launch modes, arguments, and selection persist. Deletion asks for confirmation and retains profile files in recovery trash. Duplicate copies enabled and disabled mods and their configuration on a worker thread.
- **Settings:** select Prismatic Dark, Retro Valley, or Retro Terminal. Appearance changes immediately and persists independently of the selected profile. Also choose an existing game directory manually or find conventional Steam installations, including additional libraries described by Steam metadata. Select vanilla or already-installed SMAPI. Enter one extra argument per line; a line containing spaces remains one argument. Play revalidates the executable and launches it with the correct working directory without a shell command string.
- **Mods:** install a local ZIP or folder, including a collection containing several nested mods. Each mod needs a valid `manifest.json`; name, author, version, and unique ID are displayed. Disable/enable moves the entire mod directory and preserves configuration. Removal requires confirmation and retains the directory under `RemovedMods`.
- **Installed mods:** the header opens a searchable global explorer grouped by SMAPI UniqueID. Inspect versions, descriptions, authors, dependencies, and profile-specific states; enable, disable, or remove a selected installation without changing other profiles. Filesystem changes refresh the view automatically after first opening it. Invalid manifests produce warnings while healthy installations remain visible.
- **Dependencies:** successful installations with unresolved required dependencies open an offline inspection dialog. Check dependencies also opens it at any time. Enable and launch actions warn before continuing with missing, disabled, or outdated required dependencies. Select eligible missing dependencies, then explicitly authorize the named publisher/repository before any network access. Known unsupported or installed dependencies have official manual-download links.
- **Modpacks:** import a ZIP containing multiple mods, or duplicate a configured profile as another collection. There is no online catalog or portable collection export.
- **Logs:** view/filter bounded session activity and captured game/SMAPI stdout/stderr. Export a diagnostic text file. Configured game, launcher-data, and home paths are redacted; review the contents before sharing because game output can contain other personal details. Logs are session-only unless exported.

Long profile/mod filesystem operations run off the GUI thread with progress and errors. Profile mutation and application closing are blocked during operations and while a launched game is running. Close the game normally before closing the launcher.

## What an instance isolates

An instance is a **mod profile**, not a second game installation. Game files and saves are shared. Every profile has its own `Mods` and `DisabledMods` directories. A modded launch passes its absolute Mods directory to SMAPI using `--mods-path`; the launcher's installation and profile operations never swap or overwrite the existing game's Mods folder. Vanilla ignores profile mods.

SMAPI must already be installed separately. Mods bundled into the game's original Mods directory are not automatically copied; install the mods and dependencies required by each profile. Dependency graphs are profile-local and include required/optional dependencies, content-pack frameworks, transitive metadata, cycles, and minimum versions. There is no automatic updater, SMAPI installer, or save isolation. Runtime game/SMAPI minimums are advisory when installed versions are unknown. Real game execution has deliberately not been exercised during development.

## Storage and recovery

The selected data directory contains:

```text
launcher.ini                         # optional bootstrap game/directory string
theme-preferences.json               # persistent application theme
sources.json                         # optional administrator-maintained source catalog
instances.json                       # versioned instance registry
instances.json.bak                   # previous valid registry
instances.recovery-<hash>.json        # retained malformed registry, when recovered
.launcher-session.lock               # application lifetime lock
profiles/<UUID>/Mods/
profiles/<UUID>/DisabledMods/
profiles/<UUID>/RemovedMods/<UUID>/   # recoverable explicitly removed mods
profiles/<UUID>.mods.lock             # shared profile-operation lock
trash/<UUID>-<UUID>/                  # recoverable explicitly deleted profiles
```

INI migration seeds the initial profile; subsequent instances keep independent settings. Unknown INI settings and comments are preserved when updating the bootstrap path. JSON is the current experimental metadata store; the historical SQLite plan is not implemented. See [the decision record](docs/decisions/001-local-mod-profiles.md) and [current infrastructure addendum](infrastructure_plan.md).

Registry writes use atomic replacement, backups, schema validation, stale-write detection, and recovery snapshots for malformed input. Errors preserve existing records and user files. Recovery/trash directories are not automatically purged. To recover a deleted profile, close the launcher and copy its retained mod directories into a newly created profile, or restore a consistent registry/profile backup. Do not edit registry files while the application is running.

ZIP handling uses official libarchive APIs and requires the central directory. It rejects path traversal, absolute/drive/UNC names, symlinks, hard links, special files, encrypted entries, filename aliases, duplicate paths, and conflicting mod IDs/versions. Limits are 10,000 archive entries, 256 MiB per file, 1 GiB expanded content, and bounded nesting. Installation stages files and rolls back ordinary commit failures; incomplete rollback retains staging with a recovery diagnostic. A mod containing another mod's manifest is rejected as ambiguous. Existing mod versions are never overwritten automatically, including disabled versions. To replace one, duplicate the profile for backup, explicitly remove the old version, then install its replacement.

These safeguards assume a private launcher data directory and cooperating processes. Installation does not execute archive contents. Multi-directory commits have no crash journal; atomic rename is not a guarantee against every power-loss failure. Keep backups of valuable configuration.

## Packaging and download sources

Native CMake targets `package-macos`, `package-linux`, and `package-windows` invoke the corresponding scripts. Each runs the build and CTest before deployment, validates runtime dependencies, and generates SHA-256 checksums. GitHub Actions workflows build on native runners and upload workflow artifacts; they never publish releases.

```bash
# Linux: produce a relocatable runtime bundle with installed build dependencies.
./scripts/package-linux.sh --appdir-only
./build/artifacts/linux/PrismaticLauncher.AppDir/AppRun
# Run the verified AppImage without requiring FUSE:
APPIMAGE_EXTRACT_AND_RUN=1 ./build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage
# Rebuild with supplied pinned tools: see tools/appimage/README.md.
```

On macOS run `./scripts/package-macos.sh`; on Windows run `pwsh -File scripts/package-windows.ps1 -BuildDir build/windows-release`. Native SDK prerequisites, exact commands, output paths, and signing details are in the [macOS](docs/packaging-macos.md), [Linux](docs/packaging-linux.md), and [Windows](docs/packaging-windows.md) guides. A real x86_64 AppImage was built using the supplied pinned tools and independently checked on Linux, including relocation and external persistent data. Its checksum sidecar is in `build/artifacts/linux-appimage/`. Native Apple Silicon packaging subsequently produced the ad-hoc-signed app, DMG and ZIP under `dist/macos-codex-validation/`; all 15 native tests and non-GUI package checks passed. GUI startup outside the Codex sandbox, older macOS compatibility and Windows packaging remain separate validation tasks. See the latest entries in [PROGRESS.md](PROGRESS.md).

Automatic dependency installation supports reviewed GitHub release sources in the [local source catalog](packaging/catalog/README.md), initially Content Patcher. An arbitrary `UpdateKeys` repository never establishes publisher trust. Asset selection validates repository, release version, a unique approved filename pattern, declared size, and available GitHub SHA-256 digest. The ZIP must contain exactly the approved mod ID and version and passes the existing safe transactional installer. Network and extraction run on workers. Existing installations require manual, explicitly confirmed removal before replacement. Nexus downloads remain manual; authentication and entitlements are never bypassed. Checksum sidecars and cryptographic attestations are not implemented. Live hosting availability was not tested, and ordinary CI uses mocked responses only.

## Validation and source map

```bash
./scripts/verify-infrastructure.sh
./scripts/format-check.sh
./scripts/static-analysis.sh
```

Static analysis uses compiled repository translation units from `compile_commands.json`; it includes core, UI, and tests. Set `PRISMATIC_BUILD_DIR=build/final` for another build directory. Coverage uses the coverage preset and gcovr, retaining the 60% core/config floor; gcovr must be installed separately. Docker is Linux validation infrastructure only. Its existing dependency bootstrap contacts GitHub, so Docker builds were not executed under this task's no-remote-access restriction.

- `src/core/`: atomic file handling, instance persistence, game discovery/launching, and mod services.
- `prismatic-launcher/src/qt/`: UI, styling, bootstrap configuration, and canonical entry point.
- `tests/core/`, `tests/ui/`: disposable fixture tests, adversarial archives, trusted process helper, and offscreen widget workflows. CTest redirects home/temp/data locations into the build tree.
- CMake explicitly lists target sources. `prismatic-launcher/src/qt/main.cpp` is the sole application entry point; the unused prototype window/entry point and unbuilt SMAPI helper stack were removed after a reference and target-membership audit.
- [PROGRESS.md](PROGRESS.md) records agent ownership, exact validation results, and remaining tasks/limitations.
- [Cleanup audit](docs/cleanup-audit.md) records the reference/target evidence for removed prototypes and the reasons ambiguous candidates were retained.

Public macOS Developer ID signing/notarization, Windows packaging/signing, clean-machine portability, FUSE-mounted execution, and actual game/SMAPI compatibility still require platform-specific validation. The recorded macOS development bundle is ad-hoc signed. See [PROGRESS.md](PROGRESS.md) for the latest validation evidence and [the Phase 2 report](docs/phase2-report.md) for historical implementation detail. This worktree is an integrated experiment, not a signed production release.
