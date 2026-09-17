#!/usr/bin/env bash
# Symlink MinGW runtime DLLs next to a cross-built PE so Wine can load it.
set -euo pipefail

bindir="${1:?usage: dx-stage-runtime.sh <exe-directory>}"

if [[ -z "${MINGW_GCC_LIB:-}" ]]; then
  echo "MINGW_GCC_LIB is unset (use: nix develop .#dx12)" >&2
  exit 1
fi
if [[ -z "${MINGW_MCFGTHREAD:-}" ]]; then
  echo "MINGW_MCFGTHREAD is unset (use: nix develop .#dx12)" >&2
  exit 1
fi

stage() {
  local src="$1" name="$2"
  if [[ ! -f "$src" ]]; then
    echo "missing runtime DLL: $src" >&2
    exit 1
  fi
  ln -sf "$src" "$bindir/$name"
}

stage "$MINGW_GCC_LIB/libstdc++-6.dll" libstdc++-6.dll
stage "$MINGW_GCC_LIB/libgcc_s_seh-1.dll" libgcc_s_seh-1.dll
stage "$MINGW_MCFGTHREAD/libmcfgthread-2.dll" libmcfgthread-2.dll
