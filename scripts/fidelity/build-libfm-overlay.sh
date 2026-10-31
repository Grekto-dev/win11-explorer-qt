#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
source "$ROOT/.deps/env.sh"
SDK_SOURCE="$ROOT/.deps/src/libfm-qt"
OVERLAY_SOURCE="$ROOT/.state/overlay-source/libfm-qt"
BUILD_DIR="$ROOT/.state/libfm-overlay-build"
PREFIX="$ROOT/.state/libfm-overlay-prefix"
TEST_BUILD="$ROOT/.state/transfer-overlay-build"
EXPECTED_REV="4294f29ed314388f1f6d051ae0128320354e417a"

if [[ ! -d "$SDK_SOURCE/.git" ]]; then
    echo "Missing local libfm-qt source checkout: $SDK_SOURCE" >&2
    exit 2
fi
actual_rev="$(git -C "$SDK_SOURCE" rev-parse HEAD)"
if [[ "$actual_rev" != "$EXPECTED_REV" ]]; then
    echo "Expected libfm-qt $EXPECTED_REV, got $actual_rev" >&2
    exit 2
fi

check_hash() {
    local path="$1" expected="$2" actual
    actual="$(sha256sum "$SDK_SOURCE/src/$path" | cut -d' ' -f1)"
    if [[ "$actual" != "$expected" ]]; then
        echo "Unexpected upstream source hash for $path: $actual" >&2
        exit 2
    fi
}
check_hash fileoperation.h a90f13cc3e31d76ae173dc4b1af1897eb899c30c172a6d54576768787919e10f
check_hash fileoperation.cpp aa4b6559c24c6307acf7a0720d63c639b842e8011b5107470733f11a3dd463f4
check_hash fileoperationdialog.h 034b2de12c103c2d40ae7f3389655f9a4ef952059f431d70fbf4289beea33050
check_hash core/job.h 6e60550d8cf3770484702b17c76a01b87b862a18ba9b03e461d8419b115d3c1e
check_hash core/job.cpp e4783407320e82211b420118664e4df8bf76cbc016944df5176ac4616f6ae877
license_hash="$(sha256sum "$SDK_SOURCE/LICENSE" | cut -d' ' -f1)"
if [[ "$license_hash" != "b3aa400aca6d2ba1f0bd03bd98d03d1fe7489a3bbb26969d72016360af8a5c9d" ]]; then
    echo "Unexpected upstream license text hash: $license_hash" >&2
    exit 2
fi

rm -rf "$OVERLAY_SOURCE"
mkdir -p "$(dirname "$OVERLAY_SOURCE")"
cp -a "$SDK_SOURCE" "$OVERLAY_SOURCE"
rm -rf "$OVERLAY_SOURCE/.git"
patch --directory="$OVERLAY_SOURCE" -p1 --forward < "$ROOT/patches/libfm-qt/0001-opt-in-progress-dialog.patch"

cmake -S "$OVERLAY_SOURCE" -B "$BUILD_DIR" -GNinja \
    -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_PREFIX_PATH="$PREFIX;$SDK_PREFIX;$QT_ROOT" \
    -DCMAKE_MODULE_PATH="$SDK_PREFIX/share/cmake/lxqt2-build-tools/find-modules" \
    -DXKB_INCLUDE_DIR="$SYSROOT/usr/include" \
    -DXKB_LIBRARY="$SYSROOT/usr/lib/x86_64-linux-gnu/libxkbcommon.so"
cmake --build "$BUILD_DIR" --parallel 2
cmake --install "$BUILD_DIR"

cmake -S "$ROOT/tests/transfer" -B "$TEST_BUILD" \
    -DCMAKE_PREFIX_PATH="$PREFIX;$SDK_PREFIX;$QT_ROOT" \
    -DLIBFMQT_SOURCE_DIR="$OVERLAY_SOURCE/src" \
    -DLIBFMQT_INCLUDE_DIR="$PREFIX/include/libfm-qt6"
cmake --build "$TEST_BUILD" --parallel 2
LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    QT_QPA_PLATFORM=offscreen "$TEST_BUILD/tst_explorertransfercontroller"
