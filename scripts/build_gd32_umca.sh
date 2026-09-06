#!/usr/bin/env bash

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$REPO_DIR/.." && pwd)
NUTTX_DIR="$WORKSPACE_DIR/nuttx"
TOOLS_DIR="$WORKSPACE_DIR/prebuilts/tools/linux/x86_64"
BUILD_TOOLS_DIR="$WORKSPACE_DIR/prebuilts/build-tools/linux-x86_64/bin"
PYTHON_TOOLS_DIR="$WORKSPACE_DIR/prebuilts/tools/python/bin"
KCONFIG_PYTHON_DIR="$WORKSPACE_DIR/prebuilts/tools/python/dist-packages/kconfiglib"
GCC_DIR="$WORKSPACE_DIR/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin"
BOARD_CONFIG="../vendor/gigadevice/boards/gd32f4/gd32f470v_start/configs/nsh"
BUILD_JOBS=${BUILD_JOBS:-4}
GD32_APP_DIR="$REPO_DIR/app/openvela_umca_gd32"

cleanup_app_build_files()
{
  if [ -d "$GD32_APP_DIR" ]; then
    find "$GD32_APP_DIR" -maxdepth 1 -type f \
      \( -name '*.o' -o -name '.built' -o -name '.depend' \
      -o -name 'Make.dep' \) -delete
  fi
}

trap cleanup_app_build_files EXIT

export PATH="$PYTHON_TOOLS_DIR:$BUILD_TOOLS_DIR:$TOOLS_DIR:$GCC_DIR:$PATH"
export PYTHONPATH="$KCONFIG_PYTHON_DIR${PYTHONPATH:+:$PYTHONPATH}"
export CCACHE_DIR="${CCACHE_DIR:-${TMPDIR:-/tmp}/openvela-ccache-${UID:-user}}"

cd "$NUTTX_DIR"
./tools/configure.sh -E -l "$BOARD_CONFIG"
kconfig-tweak --file .config \
  -e UMCA \
  -e UMCA_PROFILE_DISCOVERY \
  -d UMCA_BUILD_LOOPBACK \
  -e UMCA_BUILD_UART \
  -e LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32 \
  -e GD32F4_UART4 \
  -e GD32F4_UART4_SERIALDRIVER \
  --set-val UART4_BAUD 115200 \
  --set-val UART4_BITS 8 \
  --set-val UART4_PARITY 0 \
  --set-val UART4_2STOP 0 \
  -d UART4_IFLOWCONTROL \
  -d UART4_OFLOWCONTROL \
  -d GD32F4_UART4_RXDMA \
  -d GD32F4_UART4_TXDMA \
  -d UART4_RXDMA \
  -d UART4_TXDMA \
  -d GD32F4_SDIO \
  -d MMCSD_SDIO
make olddefconfig
make -j"$BUILD_JOBS"

"$GCC_DIR/arm-none-eabi-size" nuttx.elf
printf '%s\n' "GD32 UMCA artifacts:"
printf '  %s\n' "$NUTTX_DIR/nuttx.elf" "$NUTTX_DIR/nuttx.bin" \
  "$NUTTX_DIR/nuttx.hex" "$NUTTX_DIR/nuttx.map"
