# Local mod profiles and experimental persistence

Status: accepted for the local experimental worktree, 2026-10-14.

The user's implementation mission extends the infrastructure-only foundation into a functional offline launcher. The original plan is retained at `prismatic-launcher/documents/infrastructure_plan.md` as historical context. This record documents the product decisions made under the user's explicit authorization to choose reasonable implementation details.

## Instances and isolation

An instance is a named mod profile, not a separately installed copy of Stardew Valley. Game files and saves are shared. Each profile owns its own `Mods` and `DisabledMods` directories below the launcher data root. The launcher passes an absolute `--mods-path` to installed SMAPI instead of copying profile files into the game's existing Mods folder. It does not install SMAPI or change game binaries. Vanilla launches ignore profile mods.

Moving a mod between those two directories preserves its configuration and other files. Installing an existing unique ID is rejected instead of overwriting user configuration. Instance deletion retains the profile in launcher trash; it never removes game files or saves. Importing a multi-mod ZIP is the local modpack workflow. Public mod catalogs and executable downloaders are outside this implementation.

## Persistence

The historical infrastructure plan selected SQLite. For this experiment, a small versioned JSON instance registry uses atomic replacement, a recoverable backup, validation, and filesystem locking. This keeps the initial metadata model small and inspectable and avoids a second database/filesystem transaction layer. Mod manifests and profile files remain the source of installed-mod state. This is a scoped deviation from the SQLite plan, not a claim that SQLite has been implemented.

Malformed or unsafe records must not trigger destructive cleanup. Failed writes must leave the previous registry usable. Recoverable profile directories and backups are retained when a transaction cannot complete. Immutable `instances.recovery-<SHA256>.json` snapshots preserve damaged registry bytes across later edits instead of depending only on the rotating backup. All paths derived from instance identifiers require validation and containment checks. Stable sibling `<profile>.mods.lock` files coordinate installation, duplication and removal before profile directories are created or moved.

## Execution and dependencies

One C++20 executable, `prismatic-launch`, uses the existing Qt Widgets shell and an explicit `launcher-core` target. QtConcurrent handles longer filesystem operations; QProcess starts trusted user-selected game installations asynchronously without shell interpolation. The UI guards profile mutation and application shutdown while the game or an installation operation is running.

Libarchive handles ZIP decoding because Qt Core has no supported public archive extraction API. Entry paths, links, file types, duplicates, counts, and expanded sizes require validation before staged files can be committed. Archive extraction never executes binaries. Tests use only disposable fixture trees under the build directory.

The experimental launcher defaults to portable data beside its executable and supports `--data-dir`, `PRISMATIC_DATA_DIR`, and `--config` for explicit locations. Development invocations select worktree-local paths. Bootstrap INI reads parse the one supported scalar path setting from fresh bounded bytes, since Qt's shared settings cache can miss equal-size replacements with unchanged timestamps. Updates preserve unrelated settings and comments. Native packaging, signing, online catalogs, dependency resolution, and cross-platform game runtime verification remain separate work.

Atomic replacement and runtime rollback preserve data on ordinary errors. Multi-directory installation has no crash journal, and atomic rename does not guarantee parent-directory metadata durability through every power-loss scenario. This implementation does not claim crash-proof transactions.
