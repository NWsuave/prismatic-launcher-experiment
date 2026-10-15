# Current implementation infrastructure

The original approved infrastructure plan is preserved at [prismatic-launcher/documents/infrastructure_plan.md](prismatic-launcher/documents/infrastructure_plan.md). The user's autonomous product implementation mission supersedes its infrastructure-only scope for this experimental worktree. This addendum records current implementation decisions; it does not authorize remote publication.

- C++20, CMake 3.28+, Qt 6 Widgets, CTest/Qt Test, vcpkg manifests, and Linux-only Docker validation remain the foundation. Qt 6.4 is the tested minimum; Qt 6.8 remains the preferred SDK.
- One application target, `prismatic-launch`, uses the existing `launcher-ui` static library and explicit `launcher-core` sources.
- Qt Core provides paths, JSON, locking and process management. Qt Concurrent runs longer filesystem operations. Libarchive development headers and runtime are required for ZIP decoding; vcpkg declares the dependency.
- Experimental instance metadata uses versioned atomic JSON with backup recovery instead of the originally planned SQLite layer. The rationale and isolation contract are recorded in [ADR 001](docs/decisions/001-local-mod-profiles.md). SQLite is not implemented.
- Instances share game files and saves but have separate Mods/DisabledMods directories. SMAPI receives `--mods-path`; the launcher's profile operations never write to the installed game's Mods folder.
- Data defaults to an executable-adjacent portable directory; `--data-dir`, `PRISMATIC_DATA_DIR`, and `--config` allow explicit paths. Development and tests select worktree-local paths.
- Qt Test covers persistence, launch plans/process handling, archive/manifests, hostile filesystem inputs, atomic writes, and widget workflows. Fixtures never execute downloaded game or mod code.
- Formatting, clang-tidy, Cppcheck and compiler warnings remain enabled. Core coverage retains the 60% floor where gcovr is installed. Native Windows/macOS packaging, signing and actual game-runtime validation remain unverified.
- Existing workflow files are local configuration only in this task. No remote access, push, GitHub API use, release or publication is performed. Docker builds that would clone GitHub are not run under this constraint.
- See [PROGRESS.md](PROGRESS.md) for current results, known limitations and exact resumption instructions.
