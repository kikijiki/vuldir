#!/usr/bin/env bash
# Locate libmcfgthread-2.dll from the mingw cross toolchain in nix.
set -euo pipefail

if [[ -n "${MCFGTHREAD_DLL:-}" && -f "$MCFGTHREAD_DLL" ]]; then
  echo "$MCFGTHREAD_DLL"
  exit 0
fi

matches=(/nix/store/*mcfgthread*/bin/libmcfgthread-2.dll)
if [[ -f "${matches[0]:-}" ]]; then
  echo "${matches[0]}"
  exit 0
fi

echo "libmcfgthread-2.dll not found (expected /nix/store/*mcfgthread*/bin/)" >&2
exit 1
