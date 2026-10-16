#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/package-linux"
output_dir="$repo_root/build/artifacts/linux"
appdir_only=false
build_config=""
usage() {
    cat <<'USAGE'
Usage: scripts/package-linux.sh [--build-dir PATH] [--output-dir PATH] [--config CONFIG] [--appdir-only]

Builds and tests before creating a relocatable x86_64 AppDir and AppImage.
APPIMAGETOOL must identify a locally supplied appimagetool executable;
APPIMAGETOOL_SHA256 must contain its verified SHA-256. No tools are downloaded.
APPIMAGE_RUNTIME and APPIMAGE_RUNTIME_SHA256 identify its verified local x86_64 runtime.
--appdir-only builds and validates the AppDir without creating an AppImage.
SOURCE_DATE_EPOCH defaults to the last local commit timestamp.
USAGE
}
while (($#)); do
    case "$1" in
        --build-dir|--output-dir|--config)
            [[ $# -ge 2 ]] || { usage >&2; exit 2; }
            if [[ "$1" == --build-dir ]]; then build_dir="$2"
            elif [[ "$1" == --output-dir ]]; then output_dir="$2"
            else build_config="$2"; fi
            shift 2 ;;
        --appdir-only) appdir_only=true; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    echo 'Linux x86_64 is required.' >&2; exit 1;
}
for tool in cmake ctest python3 readelf ldd sha256sum desktop-file-validate rg; do
    command -v "$tool" >/dev/null || { echo "Missing prerequisite: $tool" >&2; exit 1; }
done
if [[ "$appdir_only" == false ]]; then
    [[ -n "${APPIMAGETOOL:-}" && -x "$APPIMAGETOOL" ]] || {
        echo 'Set APPIMAGETOOL to a locally supplied executable, or use --appdir-only.' >&2; exit 1;
    }
    [[ "${APPIMAGETOOL_SHA256:-}" =~ ^[[:xdigit:]]{64}$ ]] || {
        echo 'APPIMAGETOOL_SHA256 must contain its verified SHA-256.' >&2; exit 1;
    }
    actual_sha=$(sha256sum -- "$APPIMAGETOOL")
    [[ "${actual_sha%% *}" == "${APPIMAGETOOL_SHA256,,}" ]] || {
        echo 'appimagetool SHA-256 mismatch.' >&2; exit 1;
    }
    [[ -n "${APPIMAGE_RUNTIME:-}" && -f "$APPIMAGE_RUNTIME" ]] || {
        echo 'Set APPIMAGE_RUNTIME to a locally supplied x86_64 AppImage runtime.' >&2; exit 1;
    }
    [[ "${APPIMAGE_RUNTIME_SHA256:-}" =~ ^[[:xdigit:]]{64}$ ]] || {
        echo 'APPIMAGE_RUNTIME_SHA256 must contain its verified SHA-256.' >&2; exit 1;
    }
    runtime_sha=$(sha256sum -- "$APPIMAGE_RUNTIME")
    [[ "${runtime_sha%% *}" == "${APPIMAGE_RUNTIME_SHA256,,}" ]] || {
        echo 'AppImage runtime SHA-256 mismatch.' >&2; exit 1;
    }
    readelf -h "$APPIMAGE_RUNTIME" | rg -q 'Advanced Micro Devices X86-64' || {
        echo 'An x86_64 ELF AppImage runtime is required.' >&2; exit 1;
    }
fi
mkdir -p -- "$build_dir" "$output_dir"
build_dir=$(cd -- "$build_dir" && pwd)
output_dir=$(cd -- "$output_dir" && pwd)
destination="$output_dir/PrismaticLauncher.AppDir"
artifact="$output_dir/PrismaticLauncher-x86_64.AppImage"
if [[ -e "$destination" || -e "$artifact" ]]; then
    echo "Refusing to replace existing packaging output in $output_dir; select a new output directory." >&2
    exit 1
fi
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    cmake -S "$repo_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
fi
if ! rg -q '^BUILD_TESTING:BOOL=ON$' "$build_dir/CMakeCache.txt"; then
    echo 'The packaging build must configure BUILD_TESTING=ON.' >&2; exit 1
fi
if [[ -z "$build_config" ]]; then
    build_config=$(python3 -c 'import pathlib,sys; print(next((line.split("=",1)[1] for line in pathlib.Path(sys.argv[1]).read_text().splitlines() if line.startswith("CMAKE_BUILD_TYPE:STRING=")), "Release"))' "$build_dir/CMakeCache.txt")
fi
cmake --build "$build_dir" --config "$build_config" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" -C "$build_config" --output-on-failure --no-tests=error
sdk_json="$build_dir/packaging/linux/qt-sdk-$build_config.json"
qmake_executable=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["qmake"])' "$sdk_json")
qt_core=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["core"])' "$sdk_json")
[[ -x "$qmake_executable" && -f "$qt_core" ]] || { echo 'Configured Qt SDK is unavailable.' >&2; exit 1; }
mkdir -p -- "$build_dir/packaging/linux"
stage_dir=$(mktemp -d "$build_dir/packaging/linux/stage.XXXXXX")
trap 'rm -rf -- "$stage_dir"' EXIT
app_dir="$stage_dir/PrismaticLauncher.AppDir"
cmake --install "$build_dir" --config "$build_config" --prefix "$app_dir/usr"
source_binary="$build_dir/bin/prismatic-launch"
if [[ -f "$build_dir/bin/$build_config/prismatic-launch" ]]; then
    source_binary="$build_dir/bin/$build_config/prismatic-launch"
fi
python3 "$repo_root/packaging/linux/deploy.py" "$app_dir" \
    --plugins "$("$qmake_executable" -query QT_INSTALL_PLUGINS)" \
    --qt-libraries "$(dirname -- "$qt_core")" --source-binary "$source_binary"
desktop-file-validate "$app_dir/org.prismatic.launcher.desktop"
QT_QPA_PLATFORM=offscreen "$app_dir/AppRun" --version
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$repo_root" log -1 --format=%ct)}
[[ "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]] || { echo 'Invalid SOURCE_DATE_EPOCH.' >&2; exit 1; }
find "$app_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
if [[ "$appdir_only" == false ]]; then
    staged_artifact="$stage_dir/PrismaticLauncher-x86_64.AppImage"
    ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --runtime-file "$APPIMAGE_RUNTIME" "$app_dir" "$staged_artifact"
    chmod +x "$staged_artifact"
    # Validate its payload without FUSE and with no game/mod execution.
    QT_QPA_PLATFORM=offscreen APPIMAGE_EXTRACT_AND_RUN=1 "$staged_artifact" --version
    mv -- "$staged_artifact" "$artifact"
    (cd -- "$output_dir" && sha256sum PrismaticLauncher-x86_64.AppImage > PrismaticLauncher-x86_64.AppImage.sha256)
fi
mv -- "$app_dir" "$destination"
(cd -- "$output_dir" && find PrismaticLauncher.AppDir -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > PrismaticLauncher.AppDir.sha256)
echo "Validated AppDir: $destination"
if [[ "$appdir_only" == false ]]; then echo "AppImage: $artifact"; fi
