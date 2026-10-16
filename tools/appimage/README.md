# Pinned local AppImage tools

The user supplied the official x86_64 binaries in this directory. Their local
hashes, type-2 ELF headers and embedded versions were verified; no remote request
was made. `manifest.json` pins the exact supplied bytes. The binaries themselves
are ignored and are not repository source.

| File | Embedded version | SHA-256 |
| --- | --- | --- |
| `appimagetool-x86_64.AppImage` | commit `854e19e`, build 313, 2026-10-04 14:58:08 UTC | `95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6` |
| `runtime-x86_64` | type2-runtime commit `8f39b89` | `156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074` |

The tool labels itself a continuous build; the recorded commit/build/hash pins
the locally supplied version rather than relying on a changing continuous tag.
Replacing either binary requires an explicit pin update after verifying the new
upstream identity and bytes.

From the repository root:

```sh
chmod u+x tools/appimage/appimagetool-x86_64.AppImage tools/appimage/runtime-x86_64
APPIMAGETOOL="$PWD/tools/appimage/appimagetool-x86_64.AppImage" \
APPIMAGETOOL_SHA256=95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6 \
APPIMAGE_RUNTIME="$PWD/tools/appimage/runtime-x86_64" \
APPIMAGE_RUNTIME_SHA256=156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074 \
./scripts/package-linux.sh --build-dir build/linux-packaging \
    --output-dir build/artifacts/linux-appimage-new
```

Choose an unused output directory; packaging preserves previous artifacts. The
script builds/tests before packaging, verifies both pinned hashes and passes the
runtime explicitly to prevent implicit downloads. See the
[Linux packaging guide](../../docs/packaging-linux.md) for platform requirements.
