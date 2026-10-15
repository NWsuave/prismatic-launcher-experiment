# Prismatic Launcher implementation progress

Updated: 2026-10-14. All work stays in this experimental worktree. No pushes, remotes, GitHub APIs, external publication, real game/mod/save changes, or downloaded binary execution were performed.

## Current state

The launcher is implemented, integrated, and validated on Linux using the official dependency SDKs. Dev and separate clean final builds pass; all six final test executables pass; formatting, infrastructure verification and the full static-analysis script pass. No known significant defect remains from the completed independent reviews. Preserve the working implementation and the limitations below.

## Official libarchive dependency — resolved

- Installed official Ubuntu `libarchive-dev` and `libarchive13t64`: both `3.7.2-2ubuntu0.9` (library 3.7.2). `dpkg -V` reported no package differences.
- Dev and fresh final CMake caches use `/usr/include` and `/usr/lib/x86_64-linux-gnu/libarchive.so`. The executable links the installed `libarchive.so.13` runtime.
- Removed temporary fabricated declaration files and their directory `build/deps` after verifying active CMake caches and compilation databases no longer refer to them.
- Earlier workaround builds are historical experimental evidence only. Current/final validation uses official headers and runtime without SDK overrides.

## Completed features

- Persistent instance create/rename/duplicate/delete/select/search, per-instance game paths, launch modes and literal arguments; deep copy preserves mod configuration.
- Versioned JSON registry, atomic replacement, previous-valid backup, immutable damaged-record recovery snapshots, schema validation, stale-writer detection, safe UUID paths and retained deletion trash.
- Conventional Steam discovery, including library metadata; manual game selection; native vanilla/SMAPI executable detection; asynchronous shell-free launch with working directory and output/error capture.
- Isolated profile mod directories supplied through SMAPI `--mods-path` and Linux environment override. Installed game Mods and saves remain shared and are not swapped or overwritten.
- Folder/ZIP installation, nested multi-mod collections, commented/trailing-comma manifest parsing, metadata display, duplicate ID/version rejection, configuration-preserving activation and recoverable removal.
- Official libarchive seekable ZIP decoding requiring central directory, bounded extraction, unsafe path/link/type/encryption/alias rejection, staging and runtime rollback. Incomplete rollback retains staging and reports recovery path.
- Functional Qt Library, Mods, Logs, Settings and Modpacks pages; asynchronous filesystem work, progress/errors, destructive confirmations and consistent controls.
- Bounded/filterable/redacted session logs and atomic diagnostic export. Logs remain accessible without a selected profile.
- One canonical `prismatic-launch` executable, existing `launcher-ui` static library, explicit `launcher-core` and test source membership; Qt Core/Widgets/Concurrent/Test and libarchive dependency declarations.
- CLI `--data-dir`, `PRISMATIC_DATA_DIR`, `--config`, `--help`, `--version`; portable executable-adjacent default data and per-directory application lifetime lock.
- Updated README, current infrastructure addendum, repository guidance and architecture decision record.

## Architecture and ownership

Instances are mod profiles, not separate game copies. Game files/saves are shared; each `profiles/<UUID>` contains Mods/DisabledMods/RemovedMods. Stable sibling `<profile>.mods.lock` coordinates mod and instance file operations before directory creation/movement. JSON is an explicit experimental deviation from historical SQLite plans; see `docs/decisions/001-local-mod-profiles.md`.

Six real agents were resumed for implementation and final independent review:

| Agent | Owned files/work |
| --- | --- |
| instances | `src/core/InstanceStore.{h,cpp}`, storage tests; independent mod/lock/atomic review |
| launch | `src/core/GameLauncher.{h,cpp}`, launcher and atomic-file tests; independent lifecycle/portability review |
| mods | `src/core/ModManager.{h,cpp}`, module tests; archive and staging fixes |
| ui | LauncherWindow, InstancePages, LauncherConfig, style; visual/runtime review |
| tests | reliability and UI tests; independent filesystem/security review |
| build | CMake, vcpkg, Docker dependency declaration, quality scripts; official-SDK build and analysis |
| coordinator | AtomicFile, canonical bootstrap, documentation, integration review, clean final validation |

Historical duplicate entry-point/SMAPI utility files remain unbuilt reference code. Their formatting was normalized to satisfy the existing whole-source format check; no product behavior was added there.

## Defects found and fixed during independent review

- Parent symlink and raw `symlink/../` escapes in atomic writes, launch profile paths and manifest/source reads: validation now occurs before normalization or side effects.
- Mod operations recreated a moved profile before acquiring its lock, threatening failed-save rollback: lock acquisition now precedes profile creation. Missing-profile deletion also honors that stable lock.
- Rotating backups lost malformed records after repeated edits: immutable SHA256-named recovery snapshots preserve original damaged bytes.
- Rapid equal-size/equal-timestamp INI changes returned cached old game paths: bootstrap reads fresh bounded bytes with a small scalar path parser; updates preserve unrelated INI settings/comments.
- Unix legacy/incomplete SMAPI wrapper could be classified as vanilla: conservative SMAPI marker detection prevents that launch.
- UI selection during mod scanning caused stale rows: controls and selection are gated; workers are awaited on destruction.
- Incomplete multi-mod rollback could delete remaining staging files: staging is retained with a recovery diagnostic.

Each significant defect was reproduced by a failing test or disposable exploit fixture, then rechecked after its fix.

## Verification results

- Official SDK `cmake --preset dev` after clearing old dependency overrides: passed.
- Official SDK full application build: passed with GCC 13.3, Qt 6.4.2 and libarchive 3.7.2.
- `ctest --test-dir build/dev --output-on-failure`: six/six executables passed (latest 2.09 seconds). Covers GUI ZIP/folder import, CRUD, persistence, same-timestamp INI changes, redacted export, archive attacks, rollback, locking and trusted process fixtures.
- `./scripts/format-check.sh`: passed.
- `./scripts/verify-infrastructure.sh`: passed using the ordinary dev preset and official libarchive discovery, with no dependency overrides.
- Strict Cppcheck with configured warning/style/performance/portability checks: passed, zero diagnostics across 14 translation units.
- `git diff --check`: passed after the final build; changed-file list inspected. Generated builds, fixtures and SDK artifacts are ignored.
- Full configured `./scripts/static-analysis.sh`: passed, exit 0, against official headers across 14 compiled repository translation units. Strict Cppcheck produced zero diagnostics; clang-tidy emitted 940 advisory warning lines under the unchanged existing policy (chiefly style, complexity and Qt ownership suggestions). Reviewed bug-prone warnings: optional-access reports follow QtTest assertions; multiplication constants remain safely within int range; adjacent same-type arguments are API design suggestions. These advisories remain cleanup work, not a warning-free analysis claim. Log: `build/static-analysis-official.log`.
- Separate clean official-SDK configure and `cmake --build build/final --clean-first -j 2`: passed with no compiler warnings/errors. `ctest --test-dir build/final --output-on-failure --output-junit build/final-results.xml`: six/six passed in 1.73 seconds, zero failures. QtTest reports 89 passed results (including setup/cleanup) and one expected native-macOS-only skip on Linux. Logs: `build/final-configure.log`, `build/final-build.log`, `build/final-tests.log`, `build/final/Testing/Temporary/LastTest.log` and JUnit `build/final-results.xml`.
- Canonical executable `--help`: exit 0. Offscreen canonical GUI started twice using only `build/application-smoke` data; the same initial instance UUID persisted across starts. Harness intentionally terminated idle GUI processes with SIGTERM after startup; no game was launched. Evidence: `build/application-smoke/results.json` and run logs.
- Existing offscreen screenshots of Library, Mods and minimum-size Settings reviewed in `build/ui-screenshots`.

CTest isolates HOME/USERPROFILE, temp, XDG data/config/cache inside the build directory. No tests execute game/mod binaries; process tests execute only the test executable's own trusted helper.

## Remaining limits and follow-up

- Actual Stardew/SMAPI runtime and native Windows/macOS builds are deliberately unverified; Linux fixtures do not prove every platform's game dependency environment.
- No automatic SMAPI installation, online catalogs/downloads/updates, dependency solver, save isolation, portable modpack export, or native release packaging/signing.
- Duplicate mod IDs are rejected even when disabled. Replacement requires explicit removal (recoverable) before installing another version.
- Ambiguous parent/child manifests are rejected. Installation limits: 10,000 archive entries, 256 MiB/file, 1 GiB expanded content, bounded nesting.
- No multi-directory crash journal and no guarantee of parent-directory rename durability through every power-loss event. Backups/recovery data are retained.
- Session logs are not written continuously to disk; users can export them. Redaction covers known paths, not all personal details that game output might contain.
- Clang-tidy style/complexity/ownership advisory cleanup remains; no checks were suppressed or removed to get a passing result.
- gcovr is absent, so coverage generation/60% floor is not executed locally. Checks remain configured; no threshold or security checks were disabled.
- Docker not executed: its existing dependency bootstrap clones GitHub, prohibited by this task's local-only restriction.

## Resume or run

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
./build/dev/bin/prismatic-launch --data-dir "$PWD/data"
```

Official Qt and libarchive development packages are required. There is no longer a local SDK workaround. Preserve current uncommitted work; review diffs locally and do not access remotes.
