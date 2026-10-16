# Conservative cleanup and dead-code audit

Date: 2026-10-15. Baseline commit: `78917324cc15051b492d8877b05cd04c89439261`.
The working tree was clean before this audit. No commit, push, remote operation,
publication, package deletion or user-data operation is part of this cleanup.

## Evidence and removal boundary

The root and Qt CMake files explicitly enumerate all application/core/UI/test
sources. The product source lists are unconditional across supported platforms;
the platform branches define deployment settings and packaging targets, not an
alternate application. The configured compilation database contains 29 repository
translation units. Both CMake inspection and that database exclude the complete
prototype group below.

Repository-wide searches, including hidden configuration while excluding build
output and Git internals, found no active consumers of the prototype classes.
`MainWindow` references were confined to its old entry point/window/designer form;
the three SMAPI helpers referenced only each other. No headers from either group
are installed or exported. Formatting scanned these files, but no product,
packaging or test target compiled them. Their previous reference-only status was
already documented in README and development reports.

| Removed tracked file | Lines | Reason |
| --- | ---: | --- |
| `prismatic-launcher/src/main.cpp` | 10 | Alternate entry point for the unused prototype window. Canonical entry point is `src/qt/main.cpp` relative to `prismatic-launcher/`. |
| `prismatic-launcher/src/qt/MainWindow.cpp` | 9 | Empty designer-window implementation; no active target/caller. |
| `prismatic-launcher/src/qt/MainWindow.h` | 22 | Header used only by the removed prototype. |
| `prismatic-launcher/src/qt/MainWindow.ui` | 31 | Designer form consumed only by that prototype. |
| `prismatic-launcher/src/smapi/SmapiInstaller.cpp` | 170 | Unbuilt installer; the shipped launcher explicitly requires separately installed SMAPI. No active caller. |
| `prismatic-launcher/src/smapi/SmapiInstaller.h` | 50 | Types/interface consumed only by the unbuilt helper stack. |
| `prismatic-launcher/src/smapi/SmapiLauncher.cpp` | 184 | Unbuilt fork/argument-parsing launcher, superseded by the active cross-platform GameLauncher. |
| `prismatic-launcher/src/smapi/SmapiLauncher.h` | 10 | Header consumed only by that unbuilt stack. |
| `prismatic-launcher/src/smapi/SmapiManager.cpp` | 48 | Unbuilt wrapper with no active consumer. |
| `prismatic-launcher/src/smapi/SmapiManager.h` | 46 | Unbuilt wrapper/status types; no shipped header/API consumer. |
| **Total** | **580** | **10 files; no active implementation or test removed.** |

No active C++ function/class/public interface was changed. Callback references
were checked as member-pointer connections, not merely `name(...)` searches:
for example `LauncherWindow::addInstance`, `toggleMod` and
`DependencyDialog::resolveSelected` are used through Qt `connect` even though
simple textual call counts can make them look unused.

## Configuration and current documentation

- Corrected the CodeQL source-presence guard in `pr-checks.yml`. Its former
  `prismatic-launcher/ui` directory does not exist, so the job always skipped the
  actual application. The guard now searches `src`, `prismatic-launcher/src` and
  `tests`. The exact workflow shell was tested against both the real source tree
  (`has_source=true`) and a disposable empty tree (`has_source=false`). No security
  check, permission or test gate was disabled.
- Added `dist` and the two cached AppImage binary paths to `.dockerignore`.
  These were already ignored by Git, but could otherwise enter Docker's context.
  Tool manifests/docs and every packaging implementation remain included. No
  existing release package or cache was deleted.
- The `.gitignore` audit found that observed build trees, packages, tool binaries,
  coverage and local data are already covered. No new broad ignore rule was
  warranted. `git ls-files` found zero tracked generated binaries, packages,
  object files, logs, Python bytecode or build/coverage output.
- Updated current README, agent guidance and infrastructure notes to reflect the
  sole entry point and removed helper group. Current macOS guidance now recognizes
  the recorded native arm64 app/DMG build, 15 passing tests and non-GUI package
  checks, while retaining the precise GUI/sandbox and minimum-OS limitations.
  Linux verification in this audit does not establish new macOS runtime evidence.
- Historical reports and both architecture decisions are preserved. Older
  statements describing the removed files as reference sources remain historical
  evidence; the new PROGRESS entry records their removal.

## Dependencies and retained code

No library, CMake target, preset, vcpkg entry, provider contract or packaging
utility was eliminated. Each current dependency has an active role:

| Dependency | Active use |
| --- | --- |
| Qt Core | Atomic persistence, JSON, locks, paths, process management and bootstrap. |
| Qt Widgets | Canonical launcher and all dialogs/pages. |
| Qt Concurrent | Worker scans, profile operations, extraction and dependency installation. |
| Qt Network | The explicitly authorized release downloader in ModSource. |
| Qt Test | All 14 compiled test executables. |
| LibArchive | Safe ZIP handling and adversarial archive fixtures. |
| vcpkg configuration | Supported dependency/toolchain path, pinned registry and native packaging workflows. |

No temporary logging or debug instrumentation was found in active C++ paths.
Bounded launcher/SMAPI logs and diagnostic export are product features. Disposable
test fixtures and trusted process helpers are intentional verification code.

## Deferred candidates and risks

| Candidate | Evidence | Why retained / required follow-up |
| --- | --- | --- |
| `ModSourceCatalog::entries()` | Only declaration/definition found in tree. | Public interface; preserve as requested. Removing it needs a separate API decision. |
| `DependencyDialog::busyChanged` | Public Qt signal declared/emitted with no current in-tree receiver. | Observable metaobject/interface contract; no benefit sufficient to justify removal. |
| `ModSourceProvider` abstraction | GUI currently uses the concrete GitHub provider. | Explicit extension boundary for future authorized providers, including NXM; required by the Phase 2 design. |
| Existing `style.qss` and default-theme QSS | Bootstrap and LauncherTests still load the existing resource. | Not dead code. Consolidation would alter startup/style/test behavior; defer a separately tested appearance change. |
| QCoreApplication CLI preflight and macOS bundled-plugin setup | Both are called from the canonical entry point; the latter is guarded by `Q_OS_MACOS`. | Native CLI/loader support. Linux-only membership analysis is insufficient to remove platform branches. |
| AtomicFile named-save fallback | Handles restricted O_TMPFILE naming while preserving atomic rename/fsync; prior failures are documented. | Supported sandbox/filesystem error handling, not disposable instrumentation. |
| Fresh-byte bootstrap INI parser | Same-size/same-timestamp rewrite tests exercise behavior missed by Qt settings caching. | Compatibility and correctness protection; no supported-use-case proof permits removal. |
| Download temp-root canonicalization and SMAPI wrapper markers | Required by macOS system symlinks and conservative vanilla/SMAPI distinction. | Active platform/security behavior with tests and historical failure evidence. |
| Repeated test fixture writers and reliability scenarios | Similar helper shapes but differing archive/transaction/profile assertions. | Kept test-local; no tests or fixtures removed, merged or weakened. |
| Native CI SDK setup versus packaging SDK setup | Separate workflows provision platform tooling differently. | Do not deduplicate before native Windows/macOS verification; their requirements differ. |
| Docker vcpkg bootstrap | Native Linux CMake currently finds system SDKs, while the documented container also provides pinned vcpkg. | Preserved tooling decision. Removing it needs an infrastructure review and available container validation. |
| Ignored scratch harnesses, old builds and packages | Present under ignored `build/`/artifact directories. | Explicitly protected by the user's no-artifact-deletion constraint. |
| Historical reports, infrastructure plan, ADRs, issue templates and agent skills | Some contain old names or optional workflows. | Historical or externally selected interfaces; absence of a C++ caller does not make them dead. |

## Verification record

Baseline and final metrics, test/static-analysis outcomes, preserved-file hashes
and environment limitations are recorded in the cleanup entry of `PROGRESS.md`.
Raw machine-readable baseline evidence is `build/cleanup-audit-before.json`;
follow-up integrity/count evidence is `build/cleanup-audit-after.json`.

The audit intentionally leaves CMake AUTOUIC/Qt setup, native deployment helpers,
the AppImage pin manifest, archive safety checks, migration/recovery machinery,
source trust policies and all public active interfaces intact. There is no broad
architectural refactor or UI change.
