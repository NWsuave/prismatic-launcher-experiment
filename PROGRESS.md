# macOS native package validation — GUI launch pending outside Codex

Updated after native macOS packaging and non-GUI validation: 2026-10-15. The package was built on Apple Silicon with Qt 6.12.0. All **15/15 CTest entries passed**. `scripts/package-macos.sh` produced `dist/macos-codex-validation/Prismatic Launcher.app`, the versioned arm64 `.dmg`, `.app.zip`, and `SHA256SUMS`.

- The main executable and Cocoa platform plug-in are thin arm64 Mach-O binaries. The deployer audited **45 Mach-O files**, resolved all non-system imports from bundled dependencies, and reported zero additional dependencies needed after deployment. The main executable links Qt through `@loader_path/../Frameworks`; the Cocoa plug-in is present under `Contents/PlugIns/platforms`.
- `codesign --verify --deep --strict`, Info.plist validation, the deployment audit, packaged `--help`, ZIP integrity, both artifact checksums, and `hdiutil verify` all passed. The bundle is ad-hoc signed and not notarized. `LSMinimumSystemVersion` is **27.0.0**, determined from the bundled binaries; compatibility on older macOS versions is not established.
- The Codex-hosted GUI launch is **not a valid application failure result**: the reported SIGABRT occurs in HIServices `_RegisterApplication` during `NSApplication` initialization with Codex as parent, matching the reported Codex macOS sandbox issue (openai/codex#30043). The bundle's Qt frameworks and Cocoa plug-in were loaded before that abort. No application or packaging change was made to work around this host-specific GUI failure. Finder/normal-Terminal GUI startup and isolated-data behavior remain pending the user's manual launch outside Codex.
- The ordinary macOS native application GUI is not launched by the test suites. No real Stardew Valley installation or saves were accessed.

All source changes and generated artifacts remain local and uncommitted. This validation does not establish Gatekeeper/notarization behavior, GUI startup outside the Codex sandbox, or compatibility below macOS 27.0.0.

# Linux AppImage completed — Phase 2 follow-up

Updated after actual artifact generation and validation: 2026-10-15. This addendum supersedes the earlier Linux AppImage tooling/artifact limitation. Every byte of the previous Phase 2 report and earlier implementation history is preserved below it. macOS/Windows limitations remain unchanged.

## Result and artifacts

The existing native pipeline successfully produced a real, executable **type-2 x86_64 AppImage** from the final Release launcher, using the official local binaries supplied by the user. No production source or packaging-script fix was necessary. Previous AppDirs were preserved; the new output is separate.

| Artifact | Actual result |
| --- | --- |
| AppImage | `build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage` |
| Size | 45,935,096 bytes (43.8 MiB) |
| SHA-256 | `f86187cd2f1131783326549c4ea46e6a14e760d3accb3dc529ec3dd8cf2f4e4c` |
| Checksum sidecar | `build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage.sha256` |
| Companion staged AppDir | `build/artifacts/linux-appimage/PrismaticLauncher.AppDir/` — 122 MB, 150 bundled libraries plus Qt plugins/notices |
| Complete payload checksums | `build/artifacts/linux-appimage/PrismaticLauncher.AppDir.sha256` — all 371 regular files verified |

## Exact tool pins and provenance

The user supplied the official binaries under `tools/appimage/`. The lead independently checked local file hashes, ELF64 x86_64 architecture, `AI\x02` type-2 headers and embedded versions without remote requests. The source attribution comes from the supplied binaries/user provenance; no new upstream download or remote attestation was performed.

| Input | Embedded version | SHA-256 |
| --- | --- | --- |
| `tools/appimage/appimagetool-x86_64.AppImage` | commit `854e19e`, build 313, built 2026-10-04 14:58:08 UTC | `95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6` |
| `tools/appimage/runtime-x86_64` | type2-runtime commit `8f39b89`, 944632 bytes | `156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074` |

`tools/appimage/manifest.json` records the pins and `tools/appimage/README.md` provides the exact reproducible command. Although appimagetool calls itself a continuous build, the commit/build/hash pin fixes the exact supplied version. Binary tool caches are ignored by Git. Packaging verifies both hashes and supplies `--runtime-file` explicitly, so no deployment-tool download occurs implicitly.

## Actual verification and independent review

- The Linux packaging agent ran the Release build and the entire **16/16 CTest** gate successfully in **3.85 seconds**, then created the AppImage and checksum sidecars. Log: `build/linux-appimage-package.log`. Existing product/UI/dependency/safety and macOS/Linux policy tests remain enabled.
- The embedded runtime reports commit `8f39b89`, and the SquashFS payload begins at byte 944632. The entire runtime prefix matches the supplied input except the expected 16-byte `.digest_md5` field (offsets 932096–932111), populated by appimagetool. File identity uses the SHA-256 above.
- The lead and the separate QA agent verified the actual AppImage checksum and all 371 AppDir checksum entries. Actual runtime extraction succeeded. The extracted payload passed the runtime-closure validator; only the documented host glibc/graphics libraries remain external.
- The lead independently ran the real AppImage relocated into a directory/filename containing spaces, then started its offscreen GUI twice using isolated HOME/XDG/TMPDIR fixture paths. Registry bytes remained identical on reopen. Default data stayed outside the image and extracted executable directories. Idle process groups were intentionally terminated after startup; no game/mod binary was launched.
- Independent QA separately checked image ELF/type-2/runtime identity, `--version`, `--help`, no-FUSE extraction, dependency closure, relocated execution, and actual AppImage plus extracted-AppRun GUI startup. Both retained the same fixture profile UUID with empty stderr. The Linux packaging agent also completed its own relocated process-group startup harness.
- The lead repacked the **same staged AppDir** with the same pinned tool/runtime and `SOURCE_DATE_EPOCH=1792039735`; the result was **byte-identical**, with the same SHA-256. This proves the tested packaging boundary, not reproducibility across different SDKs, compilers or separately rebuilt source trees.
- This follow-up changes tool pin metadata, binary-cache ignores and documentation only. The existing packaging implementation worked without modification; no C++ or shell behavior changed. The previous successful full static-analysis result remains applicable to unchanged product sources. Final format-check, infrastructure configure verification, shell syntax and `git diff --check` all passed; the changed-file list was inspected.

Machine-readable evidence:

- `build/appimage-lead-validation/results.json`
- `build/appimage-lead-validation/reproduction.json`
- `build/qa-appimage/artifact-review.json`
- `build/qa-appimage/runtime-review.json`
- `build/qa-appimage/gui-review.json`
- `build/linux-appimage-runtime-report.json`
- `build/linux-appimage-lead-appdir-checksums.log`

[The independent review](docs/phase2-review.md) now includes its actual AppImage follow-up. The original seven-agent ownership remains documented in the preserved Phase 2 report below; this follow-up resumed the Linux packaging and QA agents while the lead handled tool pins, independent reproduction/startup verification and documentation.

## Run and rebuild

```sh
(cd build/artifacts/linux-appimage && sha256sum --check PrismaticLauncher-x86_64.AppImage.sha256)
APPIMAGE_EXTRACT_AND_RUN=1 ./build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage
```

Rebuild using the exact pinned inputs in [tools/appimage/README.md](tools/appimage/README.md), selecting a new output directory because existing artifacts are protected from replacement. The [Linux guide](docs/packaging-linux.md) contains prerequisites and the validated execution options. No GitHub contact, publication, release, commit or push occurred.

## Remaining limits

Actual validation covers Ubuntu 24.04 x86_64, the supplied runtime's no-FUSE extraction mode, bundled dependency closure, relocation and offscreen Qt startup. FUSE mounting, real X11/Wayland rendering, other distributions, clean-machine portability and actual Stardew/SMAPI execution remain unverified. appimagetool emitted a nonblocking missing AppStream metadata warning; desktop entry/icon checks passed. Native macOS/Windows artifacts, signing/notarization, live mod-hosting downloads and the earlier dependency-service limitations remain as recorded below.

---

# Preserved Phase 2 and previous implementation reports

# Prismatic Launcher — current Phase 2 development status

Updated after integration and final validation: 2026-10-15. The complete previous implementation history is preserved verbatim below this report. Phase 2 supersedes earlier statements that packaging, themes, dependencies or standard-path storage were not implemented.

The local product integration is complete and verified on Ubuntu 24.04 x86_64. Native packaging configuration is implemented for three platforms, but unavailable native environments/AppImage tools prevent claiming all requested distribution artifacts were produced. All work remains local and uncommitted.

## Implementation versus independent verification

| Workstream | Implemented | Independently verified in this session | Not verified / remaining |
| --- | --- | --- | --- |
| Existing MVP | Existing profiles, ZIP/folder import, activation, recovery, logging and launch services preserved | Lead baseline six/six tests before changes; final original core/UI suites and QA clean build pass | Real Stardew/SMAPI execution and saves deliberately untouched |
| Global mod explorer | Cross-profile ID aggregation, versions/metadata, search/filter, associated installations, profile-specific actions, automatic refresh and invalid-manifest warnings | Independent QA fixture tests cover multiple versions/profile state, isolated toggle and original Mods-page refresh; dedicated explorer/inventory suites pass | Large real-user libraries and native Windows/macOS visuals |
| Runtime themes | All three themes, Settings selector, immediate application-wide styling, safe persistent preferences and font fallbacks | Independent Settings/reopen tests and inspected screenshots; all theme/resource/contrast/failure tests pass | Native platform font/rendering differences |
| Dependency detection | Required/optional/content-pack semantics, profile-local graphs, minimums, transitive known metadata, cycles/deduplication and structured diagnostics | Resolver fixtures and independent cross-profile dialog, canceled enable and automatic post-install inspection tests pass | Installed SMAPI/game versions can remain unknown; real runtime compatibility is advisory |
| Automatic installation | Curated publisher-evidenced GitHub provider, explicit selection/consent before network, validated assets, bounded transport, digest checks, exact approved ID/version and staged missing-only install | Mocked success/failure/no-consent/unavailable/malicious ZIP/hash/version scenarios and independent substitution safety tests pass; source/trust code independently reviewed | Live release availability/downloads, sidecar checksums, cryptographic attestations, automatic upgrades; Nexus remains manual |
| Persistence migration | Qt AppLocalDataLocation plus retain-original, absent-destination-only staged migration and bounded streaming | DataPaths fixture suite passed in lead Release and independent QA clean Debug builds; packaged restarts preserve external registry | Native macOS/Windows real-user migration and power-loss durability |
| Linux packaging | Native AppDir/AppImage pipeline, exact Qt SDK/library closure, desktop/icon, environment restoration, checksums and workflow | Actual 122 MB AppDir, 371 hashes, 150 libraries, relocation with spaces, version and offscreen GUI verified by agent and lead | No AppImage tool/runtime, so no `.AppImage`; other distributions/display servers unverified |
| macOS packaging | Native app/DMG pipeline, Qt/framework/dylib deployment, runtime audit, ad-hoc/unsigned support, signing/notarization guide | Independent source review and 10 host-independent deployment-policy tests | No macOS host, `.app`/`.dmg`, native loader/signing/notarization result |
| Windows packaging | x64 ZIP pipeline, Qt/LibArchive/runtime deployment, PE closure, checksums and workflow | Independent source/guard/YAML review, including fixed junction and runtime-PATH findings | No Windows/PowerShell/MSVC host, ZIP or native execution result |
| CI/publication | Native build/test/artifact workflows; historical automatic release publisher removed | Local YAML/read-only/no-publication assertions and packaging-policy tests pass | Workflows were not executed remotely; Docker daemon access denied |


Date: 2026-10-15. All changes are local and uncommitted. No push, pull request, publication, GitHub service request, or real game/mod/save mutation was performed.

## Implemented features

- Global installed-mod explorer grouped by case-insensitive SMAPI UniqueID, retaining distinct versions, profile names, enabled states, authors, descriptions and dependency metadata. Search and state filters select profile-specific installations. Actions enable/disable or explicitly confirm recoverable removal. Scans/mutations run on workers; directory watchers refresh the activated view. Invalid manifests report warnings while preserving healthy inventory rows.
- Runtime Prismatic Dark, Retro Valley and Retro Terminal themes. Settings applies stylesheet, palette and font choices immediately. Atomic data-root preferences persist on restart; invalid settings fall back without destroying the malformed file. Major dialogs/controls use application-wide styles and keyboard-focus states.
- Profile-local dependency graphs and structured diagnostics for required/optional, missing/disabled/outdated, content-pack frameworks, runtime minimums, duplicate declarations, transitive known metadata and cycles. Minimum bounds combine to the strongest requirement. Another profile cannot satisfy a requirement. Unknown installed runtime versions remain advisory.
- Successful mod installation presents an offline dependency list when required dependencies are unresolved. Enabling and launching offer explicit warnings with affected IDs/minimums. The explorer opens dependency inspection. Known installed/outdated dependencies expose official manual pages.
- Explicitly authorized GitHub dependency downloads from a curated, publisher-evidenced catalog. Repository identity is never inferred from an arbitrary update key. Release versions, unambiguous asset policies, URL hosts, sizes, deadlines and available SHA-256 digests are validated. Downloaded archives must match exactly the approved UniqueID/version and pass the existing safe staged installer. Existing installations are never automatically replaced. New dependencies are rechecked after installation.
- Qt-standard default data storage outside executables/bundles. Legacy portable trees migrate only into absent destinations through bounded staging, with the original retained, session locks honored and links rejected. Explicit CLI/environment data roots remain available.
- Explicit native CMake packaging targets/install rules, deployment scripts, native Actions artifact workflows and SHA-256 output. The historical tag-triggered publisher was replaced by manual read-only policy validation. No workflow publishes releases automatically.

## Partial or unverified work

Packaging code exists for all three platforms, but only Linux binaries and a Linux AppDir were actually built/run here. An AppImage requires unavailable local `appimagetool` and x86_64 runtime files. macOS app/DMG and Windows ZIP creation require unavailable native environments. The scripts fail clearly rather than fabricating these artifacts. They have not been exercised on native runners in this session.

Hosted GitHub downloads were verified through local mocked responses, never through a live endpoint. The initial automatic catalog contains Content Patcher; actual current release availability/naming remains unverified. Only authenticated transport and available GitHub API SHA-256 digests are supported; checksum sidecars and cryptographic attestations are future work. Runtime SMAPI/game compatibility cannot be conclusively validated without installed version evidence. Automatic upgrades/replacements are deliberately unsupported.

## Real delegated agents and ownership

| Agent | Exclusive implementation ownership and responsibility |
| --- | --- |
| `macos_packaging` | macOS deployment script, Mach-O bundler/auditor/tests, native workflow and guide |
| `linux_packaging` | AppDir/AppImage script, deployment policy/tests, desktop/icon/integration, native workflow and guide |
| `windows_packaging` | PowerShell portable ZIP, PE runtime dependency collection, native workflow and guide; independent macOS/Linux review |
| `mod_explorer` | ModInventory, ModExplorerPage and dedicated inventory/widget tests |
| `themes` | ThemeManager, QSS resources and theme tests; independent dependency security/correctness review |
| `dependency_management` | Manifest extensions, resolver, source catalog/provider, safe dependency ZIP entry point and fixture tests |
| `quality_assurance` | Independent safety/integration tests and review record; reviewed other agents' code and reproduced defects |
| Lead | Shared interfaces, all CMake integration, canonical bootstrap/data migration, window/settings/dependency-dialog integration, launch environment isolation, architecture/docs and independent final validation |

Agents worked in the same experimental worktree with exclusive file ownership and separate build directories. No agents were simulated. See [independent review](docs/phase2-review.md) for defects found and repaired.

## Interfaces and decisions

| Interface | Responsibility |
| --- | --- |
| `DataPaths` | Standard data location and conservative legacy migration |
| `ModInventory` / `ModInventorySnapshot` | Derived cross-profile index; physical storage stays per profile |
| `ModExplorerPage` | Async inventory, profile-specific actions, dependency inspection/preflight hooks |
| `ThemeManager` | Resource loading, application-wide appearance and atomic preference persistence |
| `ModManifest` / `ModDependency` | Separate manifest metadata, runtime minimums, required/optional and content-pack dependencies |
| `DependencyResolver` / `DependencyResolution` | Profile-local graph, requests, diagnostic status and version comparison |
| `ModSourceCatalog` / `ModSourceProvider` | Local publisher trust policy and extensible provider contract |
| `GitHubReleaseProvider` | Bounded approved release lookup/download and safe missing-only installation |
| `ModManager::installDependencyZip` | Approved identity/version validation before ordinary staged commit |
| `DependencyDialog` | Offline inspection, selected-source consent, worker download/install and recheck |

[ADR 002](docs/decisions/002-phase2-mod-services-and-packaging.md) records the rationale. JSON persistence, C++20, Qt 6, vcpkg, explicit source lists, one canonical application executable, and the shared-game/separate-mod contract remain intact.

## Sources and installation safety

The bundled source catalog maps `Pathoschild.ContentPatcher` to reviewed `Pathoschild/StardewMods` releases, with the official modding wiki as identity evidence and Nexus as the official manual page. The catalog's [maintenance guide](packaging/catalog/README.md) explains independently maintained local overrides at `dataRoot/sources.json`.

Nexus is manual only. No protected scraping, API credentials, Premium bypass, third-party mirror, arbitrary repository search or NXM download is implemented. `UpdateKeys` are parsed and displayed, not treated as download authorization. The provider interface supports future authorized providers.

The existing libarchive pipeline still rejects traversal, links, unsupported file types/encryption, archive aliases, duplicate IDs, occupied destinations and extraction bombs. Staging and rollback preserve ordinary failure safety. Higher-version archives cannot substitute for the release the user approved. Tests never execute downloaded assemblies. Existing profile configuration and other profiles remain untouched after failed downloads or invalid archives. There is no crash journal or power-loss-proof multi-directory transaction guarantee.

## Security and integration defects repaired

| Finding | Final behavior and verification |
| --- | --- |
| Approved release version acted as only a minimum for archive contents. | Exact approved manifest version/ID required before commit; independent higher-version malicious substitution fixture passes. |
| Migration budget relied only on file stat sizes. | Streaming enforces actual copied-byte limits; links/active sessions fail safely, original data retained. |
| macOS deployment could mutate through an escaping bundle symlink before checking it. | Bounds/link validation now precedes mutation and repeats after framework copying; mocked escape tests prove no external write. |
| Windows staging deletion could traverse an ancestor junction. | Existing path ancestors and staging descendants are checked for reparse points before destructive staging cleanup. Native execution remains unverified. |
| AppImage library/plugin overrides could leak into external game processes. | AppRun records host values; GameLauncher restores/removes bundle overrides. Trusted launch-plan regression covers the resulting environment. |
| Ambiguous or corrupt profile contents hid healthy inventory records. | Tolerant locked inventory retains healthy mods and reports warnings; unsafe linked paths are rejected and strict mutation safety stays intact. |
| Manual-only dependency rows inherited a checkable Qt flag. | Unsupported/existing installations cannot be selected for automatic replacement; GUI tests verify official manual-page access. |
| Failed inspection could appear to report no issues. | Errors explicitly mark inspection incomplete; success text requires a successful scan. |
| Early background explorer scans disabled existing profile controls. | Main-window scanning is lazy and coordinated with profile operations; the original UI suite passes unchanged. |
| Version comparison accepted leading-zero numeric prerelease components. | Invalid numeric prereleases are rejected and covered by resolver tests. |
| Historical tag pushes could initiate release publication. | The publisher and write permission are removed; all package/release-policy workflows have read-only contents permissions and no release-creation job. |

The first complete static analysis also found six Cppcheck style/test issues. They were repaired without changing the configured checks or adding suppressions: standard algorithms replace simple loops, exception naming no longer shadows an outer variable, and the no-fetch regression directly fails if its injected transport is invoked.

## Actual artifacts and platform verification

All paths below are relative to this worktree:

| Artifact/evidence | Path and result |
| --- | --- |
| Runnable Linux runtime bundle | `build/artifacts/linux/PrismaticLauncher.AppDir/` — 122 MB, 150 bundled libraries plus plugins/notices |
| Bundle checksum manifest | `build/artifacts/linux/PrismaticLauncher.AppDir.sha256` — all 371 files verified by packaging agent and lead |
| Clean Release executable | `build/phase2-final/bin/prismatic-launch` |
| Relocation evidence | `build/linux-package-relocation.json` — moved to a path containing spaces, runtime closure/version/offscreen GUI passed |
| Independent packaged smoke | `build/phase2-packaged-smoke/results.json` — closure passed, version 0.2.0, three GUI starts, registry identical on reopen, no bundle data created |
| Theme previews | `build/phase2-final/theme-previews/{prismatic-dark,retro-valley,retro-terminal}.png` |
| AppImage, macOS app/DMG, Windows ZIP | Not produced; unavailable deployment tools/native platforms |

The final bundled executable SHA-256 is `ba7b54d2cb39fba04aa3402cd09d3aad3cacbf586d8834423b29071b436d187c`. The earlier pre-analysis-fix bundle was retained under `build/artifacts/linux-before-analysis-fixes/`; the `linux/` path is the final artifact.

The actual build/test host was Ubuntu 24.04 x86_64, GCC 13.3, Qt 6.4.2, official libarchive 3.7.2. Native macOS/Windows, other Linux distributions, X11/Wayland display-server behavior, actual game/SMAPI execution, code signing and notarization were not tested. Offscreen GUI smoke harnesses intentionally terminated idle launchers with SIGTERM; they did not launch a game.

## Automated verification

- Baseline before implementation: six/six original CTest entries passed.
- Lead clean Release configure/build in `build/phase2-final`: passed with no compiler warnings/errors. All 16 CTest entries passed in 3.33 seconds; 14 QtTest executables reported 155 passed results including setup/cleanup, zero failures and one expected native-macOS-only skip. The other two entries ran 10 macOS and 12 Linux deployment-policy tests.
- Separate clean Debug QA build: all 16 entries passed in 3.44 seconds.
- Isolated Linux packaging Release build: all 16 entries passed in 3.33 seconds before final packaging.
- Tests cover required/optional/satisfied/disabled/outdated dependencies, profile mismatch, content packs, transitive metadata, cycles, declaration deduplication, invalid manifests/versions, unavailable/failed mocked downloads, approval rejection, digest/identity/version mismatches, malicious ZIPs, rollback/preservation, theme persistence/loading, explorer actions and migration safety.
- Formatting, infrastructure configure verification, shell syntax, workflow YAML parse/publication-policy validation, and whitespace checks passed. The full configured `./scripts/static-analysis.sh` rerun passed, exit 0, across 29 compiled repository translation units. Strict Cppcheck reported zero diagnostics. Clang-tidy emitted 1,605 advisory warning lines under the unchanged policy; these include style/complexity/Qt ownership suggestions and reviewed test optional-access reports, so this is not a warning-free analysis claim. No check was disabled or suppressed to achieve success.
- Docker validation was unavailable: `docker info` returned permission denied for `/var/run/docker.sock`. No image build/run was attempted; the historical Docker dependency bootstrap also conflicts with this session's no-GitHub-access restriction.

Logs: `build/phase2-final-configure.log`, `build/phase2-final-build.log`, `build/phase2-final-tests.log`, `build/phase2-final/phase2-results.xml`, `build/phase2-final/Testing/Temporary/LastTest.log`, `build/phase2-static-analysis.log`, `build/phase2-static-analysis-final.log`, `build/linux-package-final.log`, and `build/linux-appdir-lead-checksums.log`. Build output is ignored and uncommitted.

## Build, run and package

```sh
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
./build/dev/bin/prismatic-launch --data-dir "$PWD/data"

# Native Linux: runtime bundle without unavailable AppImage tooling.
./scripts/package-linux.sh --appdir-only --output-dir build/artifacts/linux-new
./build/artifacts/linux/PrismaticLauncher.AppDir/AppRun
```

For AppImage creation, supply locally verified `APPIMAGETOOL`, `APPIMAGETOOL_SHA256`, `APPIMAGE_RUNTIME`, and `APPIMAGE_RUNTIME_SHA256`; then invoke `scripts/package-linux.sh` without `--appdir-only`. It never downloads tools implicitly. The default Linux CMake packaging target requires these prerequisites.

On macOS, configure a native Qt/LibArchive build then run `scripts/package-macos.sh`; default outputs are `dist/macos/Prismatic Launcher.app`, versioned `.dmg`/`.app.zip`, and SHA256SUMS. Ad-hoc or unsigned development packages are supported; Developer ID signing/notarization is documented, not performed.

On Windows, configure a Release x64 build, then run `pwsh -File scripts/package-windows.ps1 -BuildDir build/windows-release` with explicit Qt/runtime SDK paths as needed. Default outputs are `build/artifacts/windows/PrismaticLauncher-0.2.0-windows-x64.zip` and `.sha256`. End users are intended to receive the deployed runtime closure; clean-Windows validation remains required.

Detailed native prerequisites/options are in the [Linux](docs/packaging-linux.md), [macOS](docs/packaging-macos.md), and [Windows](docs/packaging-windows.md) guides. Existing packaging output is protected from accidental replacement.

## Remaining risks and improvements

No unresolved blocking defect was found in the locally verified implementation. Before public distribution, exercise all native workflows, verify clean-machine runtime closure and platform licenses, provide the AppImage tool/runtime, test supported distributions/display servers, and complete signing/notarization as appropriate. Add reviewed catalog entries and live opt-in provider integration verification. Future work includes authenticated NXM, signed catalog updates/attestations, installed-runtime version discovery, explicitly approved transactional upgrades, download cancellation, and crash journaling. Profiles containing malformed/duplicate manifests remain inspectable in the tolerant inventory but require repair before strict mutation services will act.

---

# Preserved previous implementation history

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
