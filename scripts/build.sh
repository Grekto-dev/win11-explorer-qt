#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
build_type="RelWithDebInfo"
if (($#)) && [[ "$1" != --* ]]; then
    build_type="$1"
    shift
fi
case "$build_type" in
    Debug|RelWithDebInfo) ;;
    *) printf 'Usage: %s [Debug|RelWithDebInfo] [--overlay-prefix PATH]\n' "$0" >&2; exit 2 ;;
esac
overlay_prefix="$repo/.state/libfm-overlay-prefix"
while (($#)); do
    case "$1" in
        --overlay-prefix)
            (($# >= 2)) || { printf '%s requires a path\n' "$1" >&2; exit 2; }
            requested_prefix="$2"
            [[ "$requested_prefix" = /* ]] || requested_prefix="$repo/$requested_prefix"
            overlay_prefix="$requested_prefix"
            shift 2 ;;
        *) printf 'Unknown option: %s\nUsage: %s [Debug|RelWithDebInfo] [--overlay-prefix PATH]\n' "$1" "$0" >&2; exit 2 ;;
    esac
done
case "$overlay_prefix" in
    "$repo"/.state/*) ;;
    *) printf 'Overlay prefix must stay inside this worktree .state/; refusing to touch shared/system prefixes: %s\n' "$overlay_prefix" >&2; exit 2 ;;
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
python="/usr/bin/python3"
if ! "$python" -c 'import PIL, Xlib' >/dev/null 2>&1; then
    printf 'CTest suite needs a Python interpreter with Pillow and python-xlib; /usr/bin/python3 is missing one. Install those dependencies or set a checked interpreter in CMake configuration.\n' >&2
    exit 1
fi
# Validate app mutation paths too, before rebuilding the dependency.
"$python" "$repo/scripts/fidelity/overlay-preflight.py" "$repo" "$repo/.deps/src/libfm-qt" \
    "$repo/.state/build" "$repo/.state/install" "$overlay_prefix"
export LIBFMQT_OVERLAY_PREFIX="$overlay_prefix"
"$repo/scripts/fidelity/build-libfm-overlay.sh"
overlay_source="$overlay_prefix/include/libfm-qt6"
if [[ ! -f "$overlay_source/core/filetransferjob.h" || ! -f "$overlay_prefix/include/libfm-qt6/fm-qt6_export.h" ]]; then
    printf 'Private libfm-qt overlay is incomplete; refusing ABI-mismatched application build.\n' >&2
    exit 1
fi
build_dir="$repo/.state/build"
install_dir="$repo/.state/install"
mkdir -p -- "$build_dir" "$install_dir"
if ! "$cmake_cmd" -S "$repo" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_INSTALL_PREFIX="$install_dir" -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    -DCMAKE_PREFIX_PATH="$overlay_prefix;${SDK_PREFIX:-};${QT_ROOT:-}" \
    -DCMAKE_MODULE_PATH="${SDK_PREFIX:-}/share/cmake/lxqt2-build-tools/find-modules" \
    -DXKB_INCLUDE_DIR="${SYSROOT:-$repo/.deps/sysroot}/usr/include" \
    -DXKB_LIBRARY="${SYSROOT:-$repo/.deps/sysroot}/usr/lib/x86_64-linux-gnu/libxkbcommon.so" \
    -DLIBFMQT_OVERLAY_PREFIX="$overlay_prefix" -DLIBFMQT_SOURCE_DIR="$overlay_source" \
    -DLIBFMQT_INCLUDE_DIR="$overlay_prefix/include/libfm-qt6" \
    -DENABLE_HEAVY_TRANSFER_TESTS=ON -DPython3_EXECUTABLE="$python"; then
    printf 'CMake configuration failed. Verify the private overlay and Qt/libfm-qt SDK; see docs/BUILDING.md.\n' >&2
    exit 1
fi
if ! "$cmake_cmd" --build "$build_dir" --parallel 2; then
    printf 'Build failed; see docs/BUILDING.md for SDK requirements.\n' >&2
    exit 1
fi
"$cmake_cmd" --install "$build_dir"
printf 'Built and installed only to %s (libfm-qt overlay: %s)\n' "$install_dir" "$overlay_prefix"
