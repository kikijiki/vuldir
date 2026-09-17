#!/usr/bin/env bash
# Idempotent Wine prefix setup (vkd3d-proton d3d12 + DXVK dxgi).
# Called from the dx12 shellHook and from just setup-dx / run-dx.
set -euo pipefail

quiet=0
if [[ "${1:-}" == "--quiet" || "${1:-}" == "-q" ]]; then
  quiet=1
fi

log() { if [[ "$quiet" -eq 0 ]]; then echo "$@"; fi; }

if [[ -z "${VKD3D_PROTON_DIR:-}" ]]; then
  echo "VKD3D_PROTON_DIR unset (use: just setup-dx / just run-dx, or nix develop .#dx12)" >&2
  exit 1
fi

export WINEPREFIX="${WINEPREFIX:-$PWD/.wine-dx12}"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d12core=n,b;d3d12=n,b;dxgi=n,b;mscoree,mshtml=}"

sys32="$WINEPREFIX/drive_c/windows/system32"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
wine_run="$script_dir/dx-wine.sh"

dll_ok() {
  local name="$1" src="$2"
  local dst="$sys32/$name.dll"
  [[ -f "$src" ]] || return 1
  [[ -L "$dst" || -f "$dst" ]] || return 1
  # Compare realpaths; store paths change across rebuilds.
  [[ "$(readlink -f "$dst")" == "$(readlink -f "$src")" ]]
}

need_prefix=0
[[ -f "$WINEPREFIX/system.reg" ]] || need_prefix=1

need_dlls=0
for name in d3d12 d3d12core; do
  dll_ok "$name" "$VKD3D_PROTON_DIR/x64/$name.dll" || need_dlls=1
done
if [[ -n "${DXVK_DIR:-}" ]]; then
  dll_ok dxgi "$DXVK_DIR/x64/dxgi.dll" || need_dlls=1
fi

if [[ "$need_prefix" -eq 0 && "$need_dlls" -eq 0 ]]; then
  log "Wine prefix ready ($WINEPREFIX)"
  exit 0
fi

if [[ "$need_prefix" -eq 1 ]]; then
  log "Creating Wine prefix at $WINEPREFIX"
  # wineboot -i returns once the request is queued and wineserver writes the
  # registry files asynchronously, so poll. It may print a harmless
  # syswow64/rundll32 c0000135 error on builds without wow64.
  WINEARCH=win64 "$wine_run" wineboot -i || true
  for _ in $(seq 1 50); do
    [[ -f "$WINEPREFIX/system.reg" ]] && break
    sleep 0.2
  done
  "$wine_run" wineserver -w || true
fi

if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
  echo "Wine prefix creation failed at $WINEPREFIX" >&2
  exit 1
fi

# vkd3d-proton's setup_vkd3d_proton.sh misdetects nixpkgs' single "wine"
# binary as WoW64 and skips the DLL install, so install them here.
install_dll() {
  local name="$1" src="$2"
  local dst="$sys32/$name.dll"
  if [[ ! -f "$src" ]]; then
    echo "$src: not found, skipping $name" >&2
    return 0
  fi
  if dll_ok "$name" "$src"; then
    return 0
  fi
  log "Installing $name.dll -> $dst"
  if [[ -e "$dst" || -L "$dst" ]] && [[ ! -e "$dst.old" ]]; then
    mv -f "$dst" "$dst.old"
  fi
  ln -sfn "$src" "$dst"
  "$wine_run" wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$name" /d native /f >/dev/null
}

for name in d3d12 d3d12core; do
  install_dll "$name" "$VKD3D_PROTON_DIR/x64/$name.dll"
done

# Wine's builtin dxgi null-derefs in CreateSwapChainForHwnd with vkd3d-proton's
# d3d12; use DXVK's dxgi like Proton does.
if [[ -z "${DXVK_DIR:-}" ]]; then
  echo "DXVK_DIR unset; skipping dxgi.dll (CreateSwapChainForHwnd may crash)" >&2
else
  install_dll dxgi "$DXVK_DIR/x64/dxgi.dll"
fi

log "Wine prefix ready ($WINEPREFIX)"
