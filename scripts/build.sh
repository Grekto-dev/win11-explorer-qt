#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
build_type="${1:-RelWithDebInfo}"
case "$build_type" in
    Debug|RelWithDebInfo) ;;
    *) printf 'Usage: %s [Debug|RelWithDebInfo]\n' "$0" >&2; exit 2 ;;
esac
if [[ -f "$repo/.deps/env.sh" ]]; then
    # shellcheck disable=SC1091
    source "$repo/.deps/env.sh"
fi
cmake_cmd="$(command -v cmake || true)"
if [[ -z "$cmake_cmd" ]]; then
    printf 'CMake not found. Install CMake and the Qt/libfm-qt SDK; see docs/BUILDING.md.\n' >&2
    exit 1
fi
if ! command -v ninja >/dev/null 2>&1 && [[ -z "${NINJA_BIN:-}" ]]; then
    printf 'Ninja not found. Install Ninja; see docs/BUILDING.md.\n' >&2
    exit 1
fi
if [[ ! -f "$repo/CMakeLists.txt" ]]; then
    printf 'Project CMakeLists.txt is missing.\n' >&2
    exit 1
fi
build_dir="$repo/.state/build"
install_dir="$repo/.state/install"
mkdir -p -- "$build_dir" "$install_dir"
if ! "$cmake_cmd" -S "$repo" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type" -DCMAKE_INSTALL_PREFIX="$install_dir" -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON; then
    printf 'CMake configuration failed. Verify Qt >= 6.6, libfm-qt 2.4 and lxqt-build-tools 2.4 are available; see docs/BUILDING.md.\n' >&2
    exit 1
fi
if ! "$cmake_cmd" --build "$build_dir" --parallel 4; then
    printf 'Build failed; see docs/BUILDING.md for SDK requirements.\n' >&2
    exit 1
fi
"$cmake_cmd" --install "$build_dir"
printf 'Built and installed only to %s\n' "$install_dir"
