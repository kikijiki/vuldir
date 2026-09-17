#!/usr/bin/env bash
# Cross-compile the DX12 sample for Windows (mingw-w64). Use: just build-dx
set -euo pipefail

config="${1:-debug}"
case "$config" in
  debug)   preset=ninja-dx-mingw-debug ;;
  release) preset=ninja-dx-mingw-release ;;
  *) echo "unknown config: $config (use debug|release)" >&2; exit 1 ;;
esac

cmake --preset ninja-dx-mingw
cmake --build --preset "$preset"
