# Prismatic Launcher

A local C++20 / Qt 6 Widgets launcher for Stardew Valley with persistent mod profiles. The Library, Mods, Logs, Settings, and Modpacks pages use real filesystem services. Development is confined to this experimental worktree; no remote publication or real game-data changes are part of validation.

## Build and run

Requirements: CMake 3.28+, Ninja or another CMake generator, a C++20 compiler, Qt 6.4+ development packages (Core, Widgets, Concurrent, Test), and the **official libarchive development package**. Qt 6.8 is the preferred SDK; Linux is tested with Qt 6.4.2. On Ubuntu the relevant native packages include `qt6-base-dev`, `libarchive-dev`, `ninja-build`, and `g++`. The vcpkg manifest declares Qt and libarchive for toolchain-based builds.

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
./build/dev/bin/prismatic-launch --data-dir "$PWD/data"
```

If Qt is outside system paths, configure with `-DCMAKE_PREFIX_PATH=/path/to/Qt/SDK`. For an existing vcpkg setup, use its CMake toolchain file. No dependency downloads or system installations are performed by these instructions automatically. Windows produces `bin/prismatic-launch.exe`; macOS produces `bin/prismatic-launch.app`. Both require native Qt/runtime deployment and are not validated on this Linux host.

The default data directory is `data` beside the executable. Select a writable location with `--data-dir` or `PRISMATIC_DATA_DIR`. Use `--config /absolute/path/launcher.ini` to read an existing bootstrap configuration; otherwise the INI lives inside the data directory. `--help` lists options. Only one application may open a given data directory at a time.

## Workflows

- **Library:** create, rename, search, select, duplicate, and delete instances. Names, game paths, launch modes, arguments, and selection persist. Deletion asks for confirmation and retains profile files in recovery trash. Duplicate copies enabled and disabled mods and their configuration on a worker thread.
- **Settings:** choose an existing game directory manually or find conventional Steam installations, including additional libraries described by Steam metadata. Select vanilla or already-installed SMAPI. Enter one extra argument per line; a line containing spaces remains one argument. Play revalidates the executable and launches it with the correct working directory without a shell command string.
- **Mods:** install a local ZIP or folder, including a collection containing several nested mods. Each mod needs a valid `manifest.json`; name, author, version, and unique ID are displayed. Disable/enable moves the entire mod directory and preserves configuration. Removal requires confirmation and retains the directory under `RemovedMods`.
- **Modpacks:** import a ZIP containing multiple mods, or duplicate a configured profile as another collection. There is no online catalog or portable collection export.
- **Logs:** view/filter bounded session activity and captured game/SMAPI stdout/stderr. Export a diagnostic text file. Configured game, launcher-data, and home paths are redacted; review the contents before sharing because game output can contain other personal details. Logs are session-only unless exported.

Long profile/mod filesystem operations run off the GUI thread with progress and errors. Profile mutation and application closing are blocked during operations and while a launched game is running. Close the game normally before closing the launcher.

## What an instance isolates

An instance is a **mod profile**, not a second game installation. Game files and saves are shared. Every profile has its own `Mods` and `DisabledMods` directories. A modded launch passes its absolute Mods directory to SMAPI using `--mods-path`; the launcher's installation and profile operations never swap or overwrite the existing game's Mods folder. Vanilla ignores profile mods.

SMAPI must already be installed separately. Mods bundled into the game's original Mods directory are not automatically copied; install the mods and dependencies required by each profile. There is no dependency solver, automatic updater, SMAPI installer, or save isolation. Real game execution has deliberately not been exercised during development.

## Storage and recovery

The portable data directory contains:

```text
launcher.ini                         # optional bootstrap game/directory string
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
- CMake explicitly lists target sources. Historical `MainWindow` and SMAPI utility files remain unbuilt reference code; there is one canonical executable.
- [PROGRESS.md](PROGRESS.md) records agent ownership, exact validation results, and remaining tasks/limitations.

Native packaging/signing, Windows/macOS execution, and actual game/SMAPI compatibility still require platform-specific validation. This worktree is an integrated experiment, not a signed production release.
