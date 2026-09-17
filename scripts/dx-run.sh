#!/usr/bin/env bash
# Run the mingw-built sample.exe under Wine. Use: just run-dx
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

config="${1:-debug}"
case "$config" in
  debug)   dir=Debug;  bin=sample-d.exe ;;
  release) dir=Release; bin=sample.exe ;;
  *) echo "unknown config: $config (use debug|release)" >&2; exit 1 ;;
esac

./scripts/dx-setup-wine.sh
exe="_build/ninja-dx-mingw/sample/${dir}/${bin}"
if [[ ! -f "$exe" ]]; then
  echo "executable not found: $exe, building..." >&2
  case "$config" in
    debug)   cmake --preset ninja-dx-mingw && cmake --build --preset ninja-dx-mingw-debug ;;
    release) cmake --preset ninja-dx-mingw && cmake --build --preset ninja-dx-mingw-release ;;
  esac
fi
if [[ ! -f "$exe" ]]; then
  echo "build failed: $exe still missing" >&2
  exit 1
fi

bindir=$(dirname "$exe")
bindir=$(cd "$bindir" && pwd)
export WINEPREFIX="${WINEPREFIX:-$root/.wine-dx12}"
# Errors are visible by default; set WINEDEBUG=-all to silence.
export WINEDEBUG="${WINEDEBUG-}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d12core=n,b;d3d12=n,b;dxgi=n,b;mscoree,mshtml=}"
for dll in d3d12 d3d12core dxgi; do
  p="$WINEPREFIX/drive_c/windows/system32/$dll.dll"
  if [[ ! -e "$p" ]]; then
    echo "missing $p, setup-dx failed?" >&2
    exit 1
  fi
done
if [[ -n "${MCFGTHREAD_DLL:-}" && -f "$MCFGTHREAD_DLL" ]]; then
  cp -f "$MCFGTHREAD_DLL" "$bindir/"
elif [[ -x "./scripts/dx-mingw-runtime.sh" ]]; then
  cp -f "$(./scripts/dx-mingw-runtime.sh)" "$bindir/"
fi
wine_run="$root/scripts/dx-wine.sh"
cd "$bindir"
echo "running: wine $bin (WINEPREFIX=$WINEPREFIX)" >&2
exec "$wine_run" wine "./$bin"
