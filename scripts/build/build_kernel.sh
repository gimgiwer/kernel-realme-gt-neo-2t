#!/usr/bin/env bash
# Universal kernel build script for Realme GT Neo 2T (RMX3357 / Dimensity 1200)
# Supports local environment and GitHub Actions CI runner

set -euo pipefail

# Determine repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/../../Makefile" ]; then
    SRC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
elif [ -f "$SCRIPT_DIR/../Makefile" ]; then
    SRC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [ -n "${GITHUB_WORKSPACE:-}" ] && [ -f "$GITHUB_WORKSPACE/Makefile" ]; then
    SRC_ROOT="$GITHUB_WORKSPACE"
else
    SRC_ROOT="$(pwd)"
fi

echo "=== Kernel Source Root: $SRC_ROOT ==="

# Tool paths and options
BUILD_TOOLS_DIR="$SRC_ROOT/scripts/build"
MAGISKBOOT="${MAGISKBOOT:-$BUILD_TOOLS_DIR/magiskboot}"
BOOT_TEMPLATE="${BOOT_TEMPLATE:-$BUILD_TOOLS_DIR/boot-rmx3357-stock-500.img}"

OUT_DIR="${OUT_DIR:-$SRC_ROOT/out}"
ARCH="${ARCH:-arm64}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
CROSS_COMPILE_ARM32="${CROSS_COMPILE_ARM32:-arm-linux-gnueabi-}"
CLANG_TRIPLE="${CLANG_TRIPLE:-aarch64-linux-gnu-}"

# Setup toolchain PATH if CLANG_PATH provided
if [ -n "${CLANG_PATH:-}" ]; then
    export PATH="$CLANG_PATH:$PATH"
fi

# Locate toolchain binaries
if [ -n "${CLANG_PATH:-}" ]; then
    CLANG_CC="$CLANG_PATH/clang"
    LD_LLD="$CLANG_PATH/ld.lld"
    LLVM_AR="$CLANG_PATH/llvm-ar"
    LLVM_NM="$CLANG_PATH/llvm-nm"
    LLVM_OBJCOPY="$CLANG_PATH/llvm-objcopy"
    LLVM_OBJDUMP="$CLANG_PATH/llvm-objdump"
    LLVM_STRIP="$CLANG_PATH/llvm-strip"
else
    CLANG_CC="$(command -v clang || true)"
    LD_LLD="$(command -v ld.lld || true)"
    LLVM_AR="$(command -v llvm-ar || true)"
    LLVM_NM="$(command -v llvm-nm || true)"
    LLVM_OBJCOPY="$(command -v llvm-objcopy || true)"
    LLVM_OBJDUMP="$(command -v llvm-objdump || true)"
    LLVM_STRIP="$(command -v llvm-strip || true)"
fi

if [ -z "$CLANG_CC" ] || [ ! -x "$CLANG_CC" ]; then
    echo "[-] ERROR: Clang binary not found. Set CLANG_PATH environment variable."
    exit 1
fi

# Setup ccache if available
if command -v ccache &>/dev/null; then
    export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$SRC_ROOT}"
    export CCACHE_COMPILERCHECK="${CCACHE_COMPILERCHECK:-content}"
    export CCACHE_SLOPPINESS="${CCACHE_SLOPPINESS:-time_macros,include_file_mtime,locale}"
    CC="ccache $CLANG_CC"
    echo "[+] Using ccache compiler wrapper: $CC"
    ccache -s 2>/dev/null || true
else
    CC="$CLANG_CC"
    echo "[+] Using direct compiler: $CC"
fi

MAKE_VARS=(
    O="$OUT_DIR"
    ARCH="$ARCH"
    CC="$CC"
    CLANG_TRIPLE="$CLANG_TRIPLE"
    CROSS_COMPILE="$CROSS_COMPILE"
    CROSS_COMPILE_ARM32="$CROSS_COMPILE_ARM32"
    LD="$LD_LLD"
    AR="$LLVM_AR"
    NM="$LLVM_NM"
    OBJCOPY="$LLVM_OBJCOPY"
    OBJDUMP="$LLVM_OBJDUMP"
    STRIP="$LLVM_STRIP"
)

# [1/5] Configuration merge
echo "::group::[1/5] Merging defconfig and custom_rmx3357.config"
echo "=== [1/5] Merging defconfig and custom_rmx3357.config ==="
mkdir -p "$OUT_DIR"
cd "$SRC_ROOT"

DEFCONFIG_BASE="${DEFCONFIG_BASE:-arch/arm64/configs/stock.config}"
CONFIG_FRAGMENT="${CONFIG_FRAGMENT:-arch/arm64/configs/custom_rmx3357.config}"

if [ ! -f "$DEFCONFIG_BASE" ]; then
    echo "::error::Base defconfig not found: $DEFCONFIG_BASE"
    exit 1
fi

if [ ! -f "$CONFIG_FRAGMENT" ]; then
    echo "::error::Config fragment not found: $CONFIG_FRAGMENT"
    exit 1
fi

ARCH="$ARCH" scripts/kconfig/merge_config.sh -m -O "$OUT_DIR" "$DEFCONFIG_BASE" "$CONFIG_FRAGMENT"
make "${MAKE_VARS[@]}" olddefconfig
echo "::endgroup::"

# [2/5] Kernel compilation
echo "::group::[2/5] Compiling Kernel Image.gz and Modules"
echo "=== [2/5] Compiling Kernel Image.gz and Modules ==="
JOBS="${JOBS:-$(nproc)}"
make "${MAKE_VARS[@]}" -j"$JOBS" Image.gz modules
echo "::endgroup::"

# [3/5] Check struct offsets via pahole
echo "::group::[3/5] Checking struct offsets via pahole"
echo "=== [3/5] Checking struct offsets via pahole ==="
if ! command -v pahole &>/dev/null; then
    echo "::error::pahole utility not found in PATH! Aborting build."
    exit 1
fi

OFFSET_LINE=$(pahole -C net_device "$OUT_DIR/vmlinux" | grep -E "dev_addr;" || true)
echo "Pahole net_device dev_addr line: $OFFSET_LINE"
DEV_ADDR_OFFSET=$(echo "$OFFSET_LINE" | sed -E 's/.*dev_addr;[[:space:]]*\/\*[[:space:]]*([0-9]+).*/\1/')
if [ "$DEV_ADDR_OFFSET" != "752" ]; then
    echo "::error::ABI error: dev_addr offset ($DEV_ADDR_OFFSET) != 752"
    exit 1
fi
echo "[+] Struct offset OK: net_device.dev_addr is 752 bytes (0x2f0)."

TASK_THREAD_LINE=$(pahole -C task_struct "$OUT_DIR/vmlinux" | grep -E "struct thread_struct[[:space:]]+thread;" || true)
echo "Pahole task_struct thread line: $TASK_THREAD_LINE"
THREAD_OFFSET=$(echo "$TASK_THREAD_LINE" | sed -E 's/.*thread;[[:space:]]*\/\*[[:space:]]*([0-9]+).*/\1/')
if [ "$THREAD_OFFSET" != "3168" ]; then
    echo "::error::ABI error: task_struct.thread offset ($THREAD_OFFSET) != 3168"
    exit 1
fi
echo "[+] Struct offset OK: task_struct.thread is 3168 bytes."
echo "::endgroup::"

# [4/5] Image size verification and module packaging
echo "::group::[4/5] Verifying Image.gz size and Collecting Modules"
echo "=== [4/5] Verifying Image.gz size and Collecting Modules ==="
IMAGE_PATH="$OUT_DIR/arch/arm64/boot/Image.gz"
if [ ! -f "$IMAGE_PATH" ]; then
    echo "::error::Kernel image not found at $IMAGE_PATH!"
    exit 1
fi

IMAGE_SIZE=$(stat -c%s "$IMAGE_PATH")
IMAGE_SIZE_MB=$(awk "BEGIN {printf \"%.2f\", $IMAGE_SIZE/1048576}")
echo "Image.gz size: $IMAGE_SIZE bytes (~$IMAGE_SIZE_MB MB)"

if [ "$IMAGE_SIZE" -gt 31457280 ]; then
    echo "::error::Image.gz exceeds 30 MB (boot partition limit is 32 MB)"
    exit 1
fi
echo "[+] Image size OK: $IMAGE_SIZE bytes fits in 32 MB boot partition."

MODULES_COUNT=$(find "$OUT_DIR" -name "*.ko" | wc -l)
echo "Compiled $MODULES_COUNT kernel modules in $OUT_DIR"
echo "::endgroup::"

# [5/5] Repacking boot image
echo "::group::[5/5] Repacking boot image"
echo "=== [5/5] Repacking boot image ==="
if [ ! -f "$BOOT_TEMPLATE" ]; then
    echo "::error::Stock boot template not found: $BOOT_TEMPLATE"
    exit 1
fi

if [ ! -x "$MAGISKBOOT" ]; then
    chmod +x "$MAGISKBOOT" 2>/dev/null || true
fi

if [ ! -x "$MAGISKBOOT" ]; then
    echo "::error::magiskboot not found or not executable: $MAGISKBOOT"
    exit 1
fi

REPACK_DIR="$OUT_DIR/repack_tmp"
rm -rf "$REPACK_DIR"
mkdir -p "$REPACK_DIR"
cd "$REPACK_DIR"

"$MAGISKBOOT" unpack -h "$BOOT_TEMPLATE"
cp "$IMAGE_PATH" kernel
"$MAGISKBOOT" repack "$BOOT_TEMPLATE" "$OUT_DIR/boot-rmx3357-neocore.img"
rm -rf "$REPACK_DIR"

BOOT_IMG_SIZE=$(stat -c%s "$OUT_DIR/boot-rmx3357-neocore.img")
echo "Generated $OUT_DIR/boot-rmx3357-neocore.img size: $BOOT_IMG_SIZE bytes"

if [ "$BOOT_IMG_SIZE" -ne 33554432 ]; then
    echo "::error::Boot image size ($BOOT_IMG_SIZE) != 33554432 (32 MB)"
    exit 1
fi
echo "[+] Boot image size OK: 33554432 bytes."

# Asset 2: Stock Fastboot rollback boot image
cp "$BOOT_TEMPLATE" "$OUT_DIR/boot-rmx3357-stock-500.img"
echo "Prepared stock rollback boot: $OUT_DIR/boot-rmx3357-stock-500.img ($(stat -c%s "$OUT_DIR/boot-rmx3357-stock-500.img") bytes)"

echo "::endgroup::"

echo "=========================================================="
echo "[+] Build and packaging completed successfully"
echo "=========================================================="
