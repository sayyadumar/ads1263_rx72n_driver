#!/usr/bin/env bash
set -euo pipefail

# ─── Paths ────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(dirname "$SCRIPT_DIR")/build"
TOOLCHAIN="$SCRIPT_DIR/rx72n-toolchain.cmake"

# ─── Toolchain on PATH ────────────────────────────────────────────────────────
TOOLCHAIN_BIN="/home/sayyad/toolchains/gcc_14.2.0.202505_rx_elf/bin"
if [[ -d "$TOOLCHAIN_BIN" ]]; then
    export PATH="$TOOLCHAIN_BIN:$PATH"
fi

echo "=== Configure (Eclipse CDT project) ==="
echo "  source : $SCRIPT_DIR"
echo "  build  : $BUILD_DIR"
echo ""

cmake \
    -S "$SCRIPT_DIR" \
    -B "$BUILD_DIR" \
    -G "Eclipse CDT4 - Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_ECLIPSE_VERSION=4.25 \
    -DCMAKE_VERBOSE_MAKEFILE=OFF

echo ""
echo "=== Build ==="
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

echo ""
echo "=== Done ==="
echo "  ELF : $BUILD_DIR/ads1263_app"
echo "  HEX : $BUILD_DIR/ads1263_app.hex"
echo "  BIN : $BUILD_DIR/ads1263_app.bin"
echo "  MAP : $BUILD_DIR/ads1263_app.map"
echo ""
echo "  Eclipse project: open $BUILD_DIR as an existing project in Eclipse CDT"
