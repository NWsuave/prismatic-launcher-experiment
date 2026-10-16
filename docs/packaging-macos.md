# macOS development packaging

The native packaging path creates a self-contained `Prismatic Launcher.app`, a compressed DMG with an Applications shortcut, an `.app.zip` for transporting the bundle with its symlinks intact, and SHA-256 checksums. It does not publish releases or contact mod hosts. A recorded Apple Silicon validation with Qt 6.12.0 produced these artifacts under `dist/macos-codex-validation/` and passed all 15 native tests, the dependency/signature audits and DMG/checksum validation. That bundle reports a minimum macOS version of 27.0.0 based on its dependencies. A Codex-hosted GUI attempt aborted during native application registration; normal Finder/Terminal GUI startup remains unverified in that record. The current Linux environment cannot repeat native macOS checks. See [PROGRESS.md](../PROGRESS.md) for the full evidence and limits.

Install Xcode command line tools, CMake 3.28+, Ninja, Python 3.9+, a matching Qt 6 SDK, and libarchive. The Qt SDK must include Widgets, Concurrent, Network, Test, the Cocoa platform plugin and `macdeployqt`. For a Homebrew libarchive SDK and an installed Qt SDK:

```bash
cmake -S . -B build/package-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/macos \
  -DLibArchive_ROOT="$(brew --prefix libarchive)"
MACDEPLOYQT=/path/to/Qt/6.8.3/macos/bin/macdeployqt \
  bash scripts/package-macos.sh
```

Alternatively, build the CMake `package-macos` target after configuring. Packaging always builds and runs CTest first, and fails if there are no tests. An existing generated `dist/macos/Prismatic Launcher.app` must be moved out before another package operation; a fresh `--output-dir` also works. `--build-dir`, `--output-dir`, `--config` and `--sign adhoc|unsigned` are supported. Run `bash scripts/package-macos.sh --help` for details.

Generated paths for version 0.2.0 on Apple Silicon are:

- `dist/macos/Prismatic Launcher.app`
- `dist/macos/PrismaticLauncher-0.2.0-macos-arm64.dmg`
- `dist/macos/PrismaticLauncher-0.2.0-macos-arm64.app.zip`
- `dist/macos/PrismaticLauncher-0.2.0-macos-arm64.SHA256SUMS`

An Intel host uses `x86_64` in artifact filenames. This script builds for the configured architecture; it does not make an Intel bundle universal. Use SDKs and dependencies built for every requested architecture before attempting a universal build. The macOS workflow uses the native architecture of its `macos-14` runner and compiler deployment target 12.0. The deployer reads every bundled Mach-O slice's actual minimum OS requirement, prints the highest requirement, and writes that value to the app's `LSMinimumSystemVersion` before signing. A newer Homebrew dependency can therefore raise the package minimum above 12.0. A successful package alone does not establish compatibility with every older macOS version.

## Deployment and verification

The CMake Runtime component installs the canonical executable as a bundle. Qt's [`macdeployqt`](https://doc.qt.io/qt-6/macos-deployment.html) deploys Qt frameworks and plugins. `packaging/macos/deploy.py` then walks every Mach-O file, recursively copies remaining non-system dylibs/frameworks such as libarchive and its compression libraries, rewrites imports relative to their importing binary, and removes absolute runtime search paths. Only `/System/Library/` and `/usr/lib/` imports remain external. Unresolved imports, conflicting dylib filenames and escaping bundle symlinks fail packaging. A final audit rejects Homebrew/build-machine imports, and the deployed executable's `--help` runs with normal/fallback DYLD library paths, library injection, Qt plugin paths and Qt platform overrides removed. `hdiutil verify` validates the resulting image.

```bash
python3 -m unittest discover -s packaging/macos -p 'test_*.py' -v
python3 packaging/macos/deploy.py 'dist/macos/Prismatic Launcher.app' --audit-only
cd dist/macos
shasum -a 256 -c PrismaticLauncher-0.2.0-macos-arm64.SHA256SUMS
open 'Prismatic Launcher.app'
```

The Python tests use disposable fake Mach-O headers and mocked tool output; they never run application, game or mod code. The native workflow builds/tests and uploads development artifacts only. Ordinary core/UI tests never contact a mod-hosting service. Runtime portability should additionally be checked on a clean Mac without Qt or Homebrew, using profile data outside the app. Default persistent data uses Qt standard application-data paths; packaging never embeds profile data.

## Signing and notarization

The default is ad-hoc signing, performed from nested Mach-O binaries through frameworks to the outer app, followed by `codesign --verify --deep --strict`. `--sign unsigned` removes existing code signatures for development use. Neither mode provides a verified publisher identity or notarization; Gatekeeper may require the operating system's explicit local approval flow. Apple Silicon generally requires at least an ad-hoc signature for executable code.

Public distribution needs an Apple Developer Program membership, a **Developer ID Application** certificate, hardened runtime, appropriate entitlements, a trusted timestamp, and Apple's notarization service. These credentials belong in an external keychain/CI secret store. They are never required or generated by this development packaging script. Sign each nested library/framework before the outer app using the Developer ID identity, `--options runtime` and `--timestamp`; submit the correctly signed archive with `xcrun notarytool submit ... --keychain-profile ... --wait`, inspect its result, then staple and validate the app before rebuilding its DMG. Sign/notarize/staple the final DMG as appropriate and regenerate checksums after all mutations. Follow Apple's [notarization workflow](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) and [code-signing guidance](https://developer.apple.com/documentation/security/code-signing-services), and test installation on a separate Mac. Packaging does not automate credential import, notarization or publication.

Review the redistribution obligations of Qt and every bundled third-party library before public distribution. The package currently carries dependency binaries; a complete distribution license/notice inventory remains release preparation work.
