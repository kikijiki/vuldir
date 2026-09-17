#!/usr/bin/env bash
set -euo pipefail

fail=0

check() {
  if command -v "$1" >/dev/null 2>&1; then
    echo "ok  $1 ($("$1" --version 2>/dev/null | head -n1 || "$1" 2>&1 | head -n1))"
  else
    echo "missing  $1" >&2
    fail=1
  fi
}

check wine
check vulkaninfo

if [[ -z "${VKD3D_PROTON_DIR:-}" ]]; then
  echo "missing  VKD3D_PROTON_DIR (use: nix develop .#dx12 on x86_64-linux)" >&2
  fail=1
else
  for dll in d3d12.dll d3d12core.dll; do
    if [[ -f "$VKD3D_PROTON_DIR/x64/$dll" ]]; then
      echo "ok  $VKD3D_PROTON_DIR/x64/$dll"
    else
      echo "missing  $VKD3D_PROTON_DIR/x64/$dll" >&2
      fail=1
    fi
  done
fi

if [[ -z "${DXVK_DIR:-}" ]]; then
  echo "missing  DXVK_DIR (use: nix develop .#dx12 on x86_64-linux)" >&2
  fail=1
else
  if [[ -f "$DXVK_DIR/x64/dxgi.dll" ]]; then
    echo "ok  $DXVK_DIR/x64/dxgi.dll"
  else
    echo "missing  $DXVK_DIR/x64/dxgi.dll" >&2
    fail=1
  fi
fi

if (( fail != 0 )); then
  exit 1
fi

echo "dx12 smoke checks passed"
