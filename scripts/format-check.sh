#!/usr/bin/env bash
set -euo pipefail

mapfile -d '' sources < <(find src prismatic-launcher/src tests -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.h' -o -name '*.hpp' \) -print0 2>/dev/null || true)

if ((${#sources[@]} == 0)); then
    echo 'No C++ source files exist yet; clang-format check is deferred.'
    exit 0
fi

clang-format --dry-run --Werror "${sources[@]}"
