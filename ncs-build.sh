#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
set -euo pipefail

variant="${1:-release}"
case "$variant" in
  release) suffix=""; generator_option="" ;;
  generator) suffix="-generator"; generator_option=" -- -DCONFIG_SMOL_TEST_GENERATOR=y" ;;
  *) echo "usage: $0 [release|generator]" >&2; exit 2 ;;
esac

script_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
case "$script_dir" in
  /mnt/[a-zA-Z]/*)
    drive="${script_dir#/mnt/}"
    drive="${drive%%/*}"
    rest="${script_dir#/mnt/$drive/}"
    repo_msys="/${drive,,}/$rest"
    ;;
  *) echo "expected the repository under a WSL /mnt/<drive> path" >&2; exit 2 ;;
esac

toolchain_wsl="${NCS_TOOLCHAIN_WSL:-/mnt/c/ncs/toolchains/936afb6332}"
toolchain_msys="${NCS_TOOLCHAIN_MSYS:-/c/ncs/toolchains/936afb6332}"
ncs_msys="${NCS_ROOT_MSYS:-/c/ncs/v3.3.1}"
if [[ ! -x "$toolchain_wsl/bin/bash.exe" ]]; then
  echo "NCS toolchain bash was not found at $toolchain_wsl/bin/bash.exe" >&2
  exit 2
fi

build_msys="$repo_msys/build-nrf54l15$suffix"
"$toolchain_wsl/bin/bash.exe" -lc \
  "export PATH='$toolchain_msys:$toolchain_msys/mingw64/bin:$toolchain_msys/bin:$toolchain_msys/opt/bin:$toolchain_msys/opt/bin/Scripts:$toolchain_msys/opt/nanopb/generator-bin:$toolchain_msys/nrfutil/bin:$toolchain_msys/opt/zephyr-sdk/arm-zephyr-eabi/bin:$toolchain_msys/opt/zephyr-sdk/riscv64-zephyr-elf/bin':\$PATH; \
export PYTHONPATH='C:\\ncs\\toolchains\\936afb6332\\opt\\bin;C:\\ncs\\toolchains\\936afb6332\\opt\\bin\\Lib;C:\\ncs\\toolchains\\936afb6332\\opt\\bin\\Lib\\site-packages'; \
export NRFUTIL_HOME='C:\\ncs\\toolchains\\936afb6332\\nrfutil\\home'; \
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr; \
export ZEPHYR_SDK_INSTALL_DIR='C:\\ncs\\toolchains\\936afb6332\\opt\\zephyr-sdk'; \
cd '$repo_msys'; \
west -z '$ncs_msys/zephyr' build -p always -b xiao_nrf54l15/nrf54l15/cpuapp '$repo_msys/firmware/nrf54l15' -d '$build_msys'$generator_option"

echo "$variant image: $script_dir/build-nrf54l15$suffix/nrf54l15/zephyr/zephyr.hex"
