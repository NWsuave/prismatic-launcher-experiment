# Phase 2 independent quality review

Reviewed on Linux x86_64, 2026-10-15. This review was performed by the separately delegated quality-assurance agent, which authored only the two Phase 2 regression suites and this record. Product and packaging fixes were made by their owning agents after review findings.

## Verification

A separate clean Debug build used GCC 13.3, Qt 6.4.2, and the installed official libarchive 3.7.2 SDK:

```sh
cmake -S . -B build/qa -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qa --clean-first -j 2
ctest --test-dir build/qa --output-on-failure
```

All 16 CTest entries passed in 3.44 seconds after the clean build. The independent run covered all original core/UI suites plus persistence migration, inventory, resolver, mocked download provider, themes, explorer, integration, and the macOS/Linux packaging-policy tests. The macOS policy suite contained 10 Python tests; the Linux policy suite contained 12. Logs are in `build/qa/Testing/Temporary/LastTest.log`. The optional JUnit invocation used during review wrote `build/qa/build/qa-results.xml` because CTest resolves its output path relative to the build directory.

`clang-format --dry-run --Werror` passed for both QA-authored C++ files, and `git diff --check` passed. Earlier incremental binaries were stale after concurrent header changes; the final clean build and successful complete run supersede those results.

The integration suite rendered actual launcher Settings under all three themes. Screenshots in `build/qa/theme-previews/` were visually inspected: controls, text, focus, navigation, and form labels remain readable after font/layout events settle. The retro themes use bundled styles and platform font fallbacks.

## Independent regression coverage

`tests/core/Phase2SafetyTests.cpp` verifies:

- Two profiles can contain different versions of the same UniqueID. Disabling/removing one installation preserves the other profile and existing game Mods/save sentinels.
- A bundle containing a valid mod and malformed manifest does not partially install; both existing profiles and their configuration survive and staging is cleaned.
- An unrelated file occupying the disabled-mod destination cannot be overwritten.
- A linked removed-mod trash directory cannot redirect a removal into another profile.
- A mocked, approved release claiming version 1.0 cannot install an archive whose manifest claims version 9.0. Existing installations in both profiles survive.

`tests/ui/Phase2IntegrationTests.cpp` verifies:

- Settings switches all three themes immediately and preserves the selection when a new LauncherWindow opens.
- A content pack in Profile B still reports its framework as missing when the framework exists only in Profile A.
- Cancelling an enable warning names the missing dependency/minimum version and leaves the installation disabled.
- A successful local folder installation automatically presents the offline missing-dependency list without initiating a download.
- An outdated catalogued Content Patcher dependency offers its official Nexus page and cannot be selected for automatic replacement.
- The header explorer aggregates versions and toggles only the selected installation; the selected profile's original Mods table refreshes accordingly.

## Findings resolved during review

| Finding | Resolution/evidence |
| --- | --- |
| An approved release version was treated only as a minimum archive version. | Dependency ZIP installation now requires matching approved version; independent higher-version fixture passes. |
| A malformed manifest hid all healthy installations in the same profile. | Inventory uses a tolerant locked read-only scan, preserves healthy entries, and reports individual warnings. Strict mutation scans remain conservative. |
| Manual dependency rows inherited Qt's checkable item flag. | Manual/unsupported rows explicitly clear the flag; GUI regression passes. |
| Failed manifest inspection could display “No dependency issues detected.” | Failed inspection reports incomplete results; success text requires a successful scan. |
| Automatic inventory startup briefly disabled existing immediate UI actions. | The main window lazily activates explorer scanning. Original LauncherTests pass unchanged. |
| Migration enforced its size limit only using stat metadata. | Migration uses bounded streaming reads/writes and retains the original tree. |
| macOS deployment validated escaping bundle symlinks after mutation. | Validation now precedes mkdir/copy/chmod and repeats after framework copies; packaging regression proves no external write. |
| Windows staging bounds were lexical and could traverse an ancestor junction. | Packaging checks every existing worktree ancestor for reparse points and rejects linked staging descendants before deletion. Native execution remains unverified. |
| Standalone Windows pre-package tests omitted explicit runtime search directories. | Temporary test PATH includes supplied RuntimeDirectories. |
| Linux packaging invoked ripgrep without listing the prerequisite. | Both prerequisite checks and workflow dependency installation now include it. |

## Review boundaries and remaining limits

- No real game, save, downloaded mod assembly, GitHub release endpoint, Nexus download endpoint, or unverified mirror was used during this review. Provider tests inject local responses. GUI tests inspect offline dialogs and never authorize a real network fetch.
- The download trust boundary is a locally maintained curated policy, explicit user consent, HTTPS repository/asset checks, bounded fetches, exact manifest identity/version, and the existing staging installer. Publisher-provided SHA-256 digests are checked when present. Attestation signature verification and a live end-to-end hosted download were not exercised.
- New source catalog entries are local trust decisions; the application does not independently establish arbitrary publisher identities. Arbitrary update keys never automatically authorize a repository. Nexus remains manual and no authentication or entitlement restrictions are bypassed.
- Existing dependency installations are not automatically replaced. Disabled/outdated installations retain their files and expose manual actions/pages. Transitive metadata is resolved when available; newly installed dependencies are rechecked, and additional downloads require a fresh explicit selection.
- The installer preserves files and rolls back ordinary multi-mod failures; the existing lack of a crash journal remains. No claim of power-loss-proof transactions is made.
- Native macOS app/DMG and Windows ZIP execution was unavailable on this Linux host. Their source and host-independent policy tests were reviewed, which does not establish native packaging success. AppImage/AppDir artifact generation and portability evidence are reported separately by the lead/Linux packaging agent.
- Actual game/SMAPI launching, cross-distribution Linux compatibility, Developer ID signing/notarization, Windows signing, and third-party publisher binary behavior remain unverified.

## Actual AppImage follow-up validation

Later on 2026-10-15, the user supplied local x86_64 deployment tooling and authorized its use. Linux packaging then produced an actual type-2 AppImage. This follow-up supersedes the earlier tooling-availability limitation for Linux; native macOS/Windows and cross-distribution graphics/runtime validation remain unverified.

The independent QA agent verified these files without accessing a network or executing any game/mod binary:

| File | SHA-256 |
| --- | --- |
| `tools/appimage/appimagetool-x86_64.AppImage` | `95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6` |
| `tools/appimage/runtime-x86_64` | `156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074` |
| `build/artifacts/linux-appimage/PrismaticLauncher-x86_64.AppImage` | `f86187cd2f1131783326549c4ea46e6a14e760d3accb3dc529ec3dd8cf2f4e4c` |

The AppImage is 45,935,096 bytes. Its ELF header identifies x86_64 and contains the `AI\x02` type-2 marker. The embedded runtime reports commit `8f39b89`, the payload offset is 944632, and the SquashFS `hsqs` marker begins at that offset. Comparing the entire runtime prefix against the supplied runtime found exactly the expected 16 changed bytes at offsets 932096–932111 in ELF section `.digest_md5`, which appimagetool fills during packaging; every other runtime byte matches. Artifact identity is verified with SHA-256.

Both the AppImage checksum sidecar and all 371 regular-file AppDir checksum entries passed verification. With `APPIMAGE_EXTRACT_AND_RUN=1`, the actual AppImage passed `--version` and `--help` without FUSE. Explicit payload extraction succeeded. The extracted payload passed the runtime-closure verifier; only seven allowlisted host libraries remained: glibc's libc/libm/libresolv and the EGL/GLX/GLdispatch/OpenGL graphics libraries. A copied AppImage in a directory and filename containing spaces also returned `Prismatic Launcher 0.2.0` successfully.

The actual AppImage and its extracted `AppRun` each started the GUI with the offscreen Qt platform and isolated HOME/XDG/TMPDIR paths. Both retained the same profile registry/UUID at `build/qa-appimage/data/Prismatic/Prismatic Launcher/instances.json`. No default data directory appeared beside the AppImage or inside its extracted executable/payload directories. Each idle GUI process group was intentionally terminated after startup, with empty stderr; this is startup evidence, not a normal interactive-exit or game-launch claim. The image's SHA-256 remained unchanged after these checks.

Machine-readable evidence is retained in `build/qa-appimage/artifact-review.json`, `runtime-review.json`, and `gui-review.json`. Validation covers the available Ubuntu 24.04 x86_64 host, no-FUSE extraction, bundled dependency closure, path relocation, and offscreen GUI startup. FUSE mounting, actual X11/Wayland rendering, and other Linux distributions remain unverified.
