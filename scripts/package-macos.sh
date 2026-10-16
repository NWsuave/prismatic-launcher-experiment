#!/usr/bin/env bash
# Native development packaging only. This script never publishes artifacts.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_dir/build/package-macos"
output_dir="$repo_dir/dist/macos"
build_config=Release
sign_mode=adhoc

usage() {
    cat <<'EOF'
Usage: scripts/package-macos.sh [options]
  --build-dir DIR      Configured CMake build (default: build/package-macos)
  --output-dir DIR     Generated artifacts (default: dist/macos)
  --config NAME        CMake configuration (default: Release)
  --sign MODE          adhoc (default) or unsigned; neither is notarized
  --help               Show this help

Configure first with BUILD_TESTING=ON and an installed Qt SDK + libarchive.
MACDEPLOYQT may identify a specific Qt SDK's macdeployqt executable.
EOF
}

while (($#)); do
    case "$1" in
        --build-dir|--output-dir|--config|--sign)
            if (($# < 2)); then echo "Missing value for $1" >&2; exit 2; fi
            case "$1" in
                --build-dir) build_dir="$2" ;;
                --output-dir) output_dir="$2" ;;
                --config) build_config="$2" ;;
                --sign) sign_mode="$2" ;;
            esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != Darwin ]]; then
    echo 'macOS packaging requires a native macOS host; no package was produced.' >&2
    exit 1
fi
if [[ "$sign_mode" != adhoc && "$sign_mode" != unsigned ]]; then
    echo '--sign must be adhoc or unsigned.' >&2
    exit 2
fi
for tool in cmake ctest python3 hdiutil ditto shasum otool install_name_tool codesign; do
    command -v "$tool" >/dev/null || { echo "Required tool unavailable: $tool" >&2; exit 1; }
done
macdeployqt="${MACDEPLOYQT:-$(command -v macdeployqt || true)}"
if [[ ! -x "$macdeployqt" ]]; then
    echo 'Set MACDEPLOYQT to the macdeployqt from the Qt SDK used for this build.' >&2
    exit 1
fi
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "Configure CMake before packaging: $build_dir/CMakeCache.txt is absent." >&2
    exit 1
fi

cmake --build "$build_dir" --config "$build_config" --parallel
ctest --test-dir "$build_dir" -C "$build_config" --output-on-failure --no-tests=error
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Unexpected/missing project version in CMake cache: $version" >&2
    exit 1
fi
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
app_name="Prismatic Launcher.app"
if [[ -e "$output_dir/$app_name" ]]; then
    echo "Refusing to overwrite $output_dir/$app_name; move the previous generated app first." >&2
    exit 1
fi
stage_dir="$(mktemp -d "$output_dir/.package-macos.XXXXXX")"
trap 'rm -rf "$stage_dir"' EXIT
cmake --install "$build_dir" --config "$build_config" --prefix "$stage_dir/install" --component Runtime
source_app="$stage_dir/install/prismatic-launch.app"
if [[ ! -d "$source_app" ]]; then
    echo "CMake Runtime installation did not create $source_app" >&2
    exit 1
fi
mkdir -p "$stage_dir/image"
app_path="$stage_dir/image/$app_name"
ditto "$source_app" "$app_path"
"$macdeployqt" "$app_path" -always-overwrite -verbose=2
python3 "$repo_dir/packaging/macos/deploy.py" "$app_path" --sign "$sign_mode"
python3 "$repo_dir/packaging/macos/deploy.py" "$app_path" --audit-only
# CLI exit exercises the bundled loader and Qt platform plugin without creating user data.
env -u DYLD_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH \
    -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FALLBACK_FRAMEWORK_PATH -u DYLD_INSERT_LIBRARIES \
    -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u QT_QPA_PLATFORM -u QT_QPA_PLATFORMTHEME \
    "$app_path/Contents/MacOS/prismatic-launch" --help
ln -s /Applications "$stage_dir/image/Applications"
arch="$(uname -m)"
artifact_name="PrismaticLauncher-$version-macos-$arch"
hdiutil create -volname 'Prismatic Launcher' -srcfolder "$stage_dir/image" \
    -format UDZO -ov "$stage_dir/$artifact_name.dmg"
hdiutil verify "$stage_dir/$artifact_name.dmg"
# ZIP preserves the .app symlinks/resource metadata for CI artifact transport.
ditto -c -k --sequesterRsrc --keepParent "$app_path" "$stage_dir/$artifact_name.app.zip"
mv "$stage_dir/$artifact_name.dmg" "$output_dir/$artifact_name.dmg"
mv "$stage_dir/$artifact_name.app.zip" "$output_dir/$artifact_name.app.zip"
# Keep a directly usable .app as well as the DMG and transport ZIP.
ditto "$app_path" "$output_dir/$app_name"
(
    cd "$output_dir"
    shasum -a 256 "$artifact_name.dmg" "$artifact_name.app.zip" > "$artifact_name.SHA256SUMS"
)
echo "Created $output_dir/$app_name"
echo "Created $output_dir/$artifact_name.dmg ($sign_mode, not notarized)"
