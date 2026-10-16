# Prismatic Launcher Phase 2 implementation report

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

Packaging code exists for all three platforms. Linux binaries, a relocatable AppDir and an actual type-2 x86_64 AppImage were built and verified here. The user subsequently supplied official AppImage tools, pinned by commit/build and SHA-256 in `tools/appimage/manifest.json`. macOS app/DMG and Windows ZIP creation still require unavailable native environments. Native Actions workflows have not been exercised remotely in this session. Linux FUSE mounting, real display-server rendering and other distributions remain unverified.

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

Agents worked in the same experimental worktree with exclusive file ownership and separate build directories. No agents were simulated. See [independent review](phase2-review.md) for defects found and repaired.

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

[ADR 002](decisions/002-phase2-mod-services-and-packaging.md) records the rationale. JSON persistence, C++20, Qt 6, vcpkg, explicit source lists, one canonical application executable, and the shared-game/separate-mod contract remain intact.

## Sources and installation safety

The bundled source catalog maps `Pathoschild.ContentPatcher` to reviewed `Pathoschild/StardewMods` releases, with the official modding wiki as identity evidence and Nexus as the official manual page. The catalog's [maintenance guide](../packaging/catalog/README.md) explains independently maintained local overrides at `dataRoot/sources.json`.

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
| Actual Linux type-2 AppImage | `build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage` — 45,935,096 bytes (43.8 MiB) |
| AppImage checksum | `build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage.sha256` — verified by lead and independent QA |
| AppImage runtime/startup/reproduction evidence | `build/appimage-lead-validation/{results,reproduction}.json`, `build/linux-appimage-runtime-report.json`, and `build/qa-appimage/{artifact-review,runtime-review,gui-review}.json` |
| macOS app/DMG, Windows ZIP | Not produced; unavailable native platforms |

The final bundled executable SHA-256 is `ba7b54d2cb39fba04aa3402cd09d3aad3cacbf586d8834423b29071b436d187c`. The earlier pre-analysis-fix bundle was retained under `build/artifacts/linux-before-analysis-fixes/`; the `linux/` path retains the final standalone AppDir; `linux-appimage/` contains the completed AppImage and its staged AppDir.

The AppImage SHA-256 is `f86187cd2f1131783326549c4ea46e6a14e760d3accb3dc529ec3dd8cf2f4e4c`. Tool commit `854e19e` (build 313) embedded type2-runtime commit `8f39b89`; its 944632-byte prefix matches the pinned runtime except for the generated 16-byte `.digest_md5` section. Repacking the same AppDir with the same pinned inputs and `SOURCE_DATE_EPOCH=1792039735` reproduced the identical SHA-256. See [the tool pins](../tools/appimage/README.md).

The actual build/test host was Ubuntu 24.04 x86_64, GCC 13.3, Qt 6.4.2, official libarchive 3.7.2. Native macOS/Windows, other Linux distributions, X11/Wayland display-server behavior, actual game/SMAPI execution, code signing and notarization were not tested. Offscreen GUI smoke harnesses intentionally terminated idle launchers with SIGTERM; they did not launch a game.

## Automated verification

- Baseline before implementation: six/six original CTest entries passed.
- Lead clean Release configure/build in `build/phase2-final`: passed with no compiler warnings/errors. All 16 CTest entries passed in 3.33 seconds; 14 QtTest executables reported 155 passed results including setup/cleanup, zero failures and one expected native-macOS-only skip. The other two entries ran 10 macOS and 12 Linux deployment-policy tests.
- Separate clean Debug QA build: all 16 entries passed in 3.44 seconds.
- Isolated Linux AppDir Release build: all 16 entries passed in 3.33 seconds. The subsequent actual AppImage packaging gate passed all 16 entries in 3.85 seconds. The lead and separate QA checked actual image headers, checksums, extracted runtime closure, relocation, no-FUSE execution, offscreen startup and standard-path persistence. No product-source changes were required to complete AppImage generation.
- Tests cover required/optional/satisfied/disabled/outdated dependencies, profile mismatch, content packs, transitive metadata, cycles, declaration deduplication, invalid manifests/versions, unavailable/failed mocked downloads, approval rejection, digest/identity/version mismatches, malicious ZIPs, rollback/preservation, theme persistence/loading, explorer actions and migration safety.
- Formatting, infrastructure configure verification, shell syntax, workflow YAML parse/publication-policy validation, and whitespace checks passed. The full configured `./scripts/static-analysis.sh` rerun passed, exit 0, across 29 compiled repository translation units. Strict Cppcheck reported zero diagnostics. Clang-tidy emitted 1,605 advisory warning lines under the unchanged policy; these include style/complexity/Qt ownership suggestions and reviewed test optional-access reports, so this is not a warning-free analysis claim. No check was disabled or suppressed to achieve success.
- Docker validation was unavailable: `docker info` returned permission denied for `/var/run/docker.sock`. No image build/run was attempted; the historical Docker dependency bootstrap also conflicts with this session's no-GitHub-access restriction.

Logs: `build/phase2-final-configure.log`, `build/phase2-final-build.log`, `build/phase2-final-tests.log`, `build/phase2-final/phase2-results.xml`, `build/phase2-final/Testing/Temporary/LastTest.log`, `build/phase2-static-analysis.log`, `build/phase2-static-analysis-final.log`, `build/linux-package-final.log`, `build/linux-appdir-lead-checksums.log`, `build/linux-appimage-package.log`, and `build/linux-appimage-lead-appdir-checksums.log`. Build output is ignored and uncommitted.

## Build, run and package

```sh
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
./build/dev/bin/prismatic-launch --data-dir "$PWD/data"

# Native Linux: optional standalone runtime bundle.
./scripts/package-linux.sh --appdir-only --output-dir build/artifacts/linux-new
./build/artifacts/linux/PrismaticLauncher.AppDir/AppRun
```

The supplied tools are now pinned locally. Use the exact rebuild command in [tools/appimage/README.md](../tools/appimage/README.md), which passes `APPIMAGETOOL`, `APPIMAGETOOL_SHA256`, `APPIMAGE_RUNTIME`, and `APPIMAGE_RUNTIME_SHA256` to `scripts/package-linux.sh`. Choose an unused output directory. Run the completed image without FUSE using `APPIMAGE_EXTRACT_AND_RUN=1 ./build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage`. Packaging never downloads tools implicitly.

On macOS, configure a native Qt/LibArchive build then run `scripts/package-macos.sh`; default outputs are `dist/macos/Prismatic Launcher.app`, versioned `.dmg`/`.app.zip`, and SHA256SUMS. Ad-hoc or unsigned development packages are supported; Developer ID signing/notarization is documented, not performed.

On Windows, configure a Release x64 build, then run `pwsh -File scripts/package-windows.ps1 -BuildDir build/windows-release` with explicit Qt/runtime SDK paths as needed. Default outputs are `build/artifacts/windows/PrismaticLauncher-0.2.0-windows-x64.zip` and `.sha256`. End users are intended to receive the deployed runtime closure; clean-Windows validation remains required.

Detailed native prerequisites/options are in the [Linux](packaging-linux.md), [macOS](packaging-macos.md), and [Windows](packaging-windows.md) guides. Existing packaging output is protected from accidental replacement.

## Remaining risks and improvements

No unresolved blocking defect was found in the locally verified implementation. Before public distribution, exercise all native workflows, verify clean-machine runtime closure and platform licenses, test FUSE mounting and supported distributions/display servers, and complete signing/notarization as appropriate. Add reviewed catalog entries and live opt-in provider integration verification. Future work includes authenticated NXM, signed catalog updates/attestations, installed-runtime version discovery, explicitly approved transactional upgrades, download cancellation, and crash journaling. Profiles containing malformed/duplicate manifests remain inspectable in the tolerant inventory but require repair before strict mutation services will act.
