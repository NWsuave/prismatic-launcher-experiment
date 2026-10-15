#!/usr/bin/env bash
set -euo pipefail

build_directory="${PRISMATIC_BUILD_DIR:-build/dev}"
if [[ ! -f "$build_directory/compile_commands.json" && -f build/coverage/compile_commands.json ]]; then
    build_directory=build/coverage
fi
if [[ ! -f "$build_directory/compile_commands.json" && -z "${PRISMATIC_BUILD_DIR:-}" ]]; then
    cmake --preset dev
    cmake --build --preset dev
fi
test -f "$build_directory/compile_commands.json" || {
    echo 'Configure and build with CMAKE_EXPORT_COMPILE_COMMANDS=ON before analysis.' >&2
    exit 1
}
mapfile -d '' sources < <(python3 - "$build_directory/compile_commands.json" <<'PY'
import json
from pathlib import Path
import sys

root = Path.cwd().resolve()
source_roots = (root / "src", root / "prismatic-launcher/src", root / "tests")
with open(sys.argv[1], encoding="utf-8") as database:
    commands = json.load(database)
sources = set()
for command in commands:
    source = Path(command["file"])
    if not source.is_absolute():
        source = Path(command["directory"]) / source
    source = source.resolve()
    if any(source.is_relative_to(directory) for directory in source_roots):
        sources.add(str(source))
for source in sorted(sources):
    sys.stdout.write(source + "\0")
PY
)
if ((${#sources[@]} == 0)); then
    echo 'No compiled repository C++ sources found in the compilation database.' >&2
    exit 1
fi
clang-tidy -p "$build_directory" --config-file=.clang-tidy "${sources[@]}"
cppcheck --project="$build_directory/compile_commands.json" --file-filter='prismatic-launcher/src/qt/*' --file-filter='src/*' --file-filter='tests/*' $(< cppcheck.cfg)
