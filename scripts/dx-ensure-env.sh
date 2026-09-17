#!/usr/bin/env bash
# Exec the given command with Wine + vkd3d-proton + DXVK available, re-entering
# `nix develop .#dx12` if the current shell lacks them.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

dx12_env_ok() {
  command -v wine >/dev/null 2>&1 \
    && [[ -n "${VKD3D_PROTON_DIR:-}" && -f "${VKD3D_PROTON_DIR}/x64/d3d12.dll" ]] \
    && [[ -n "${DXVK_DIR:-}" && -f "${DXVK_DIR}/x64/dxgi.dll" ]]
}

if [[ $# -lt 1 ]]; then
  echo "usage: dx-ensure-env.sh <command> [args...]" >&2
  exit 2
fi

if dx12_env_ok; then
  export WINEPREFIX="${WINEPREFIX:-$root/.wine-dx12}"
  export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d12core=n,b;d3d12=n,b;dxgi=n,b;mscoree,mshtml=}"
  exec "$@"
fi

if [[ "${VULDIR_DX12_REEXEC:-}" == "1" ]]; then
  echo "DX12 environment incomplete after nix develop .#dx12." >&2
  echo "Need wine on PATH, VKD3D_PROTON_DIR (d3d12.dll), and DXVK_DIR (dxgi.dll)." >&2
  exit 1
fi

if ! command -v nix >/dev/null 2>&1; then
  echo "DX12 environment missing (wine/vkd3d/DXVK). Run: nix develop .#dx12" >&2
  exit 1
fi

echo "DX12 env missing in this shell; entering nix develop .#dx12 ..." >&2
export VULDIR_DX12_REEXEC=1
cd "$root"
exec nix develop "${root}#dx12" --command "$@"
