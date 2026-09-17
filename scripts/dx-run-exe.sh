#!/usr/bin/env bash
# Run a Windows-built sample.exe under Wine. Use: just run-exe-dx <path>
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

exe="${1:?usage: dx-run-exe.sh <path-to-exe>}"
if [[ ! -f "$exe" ]]; then
  echo "executable not found: $exe" >&2
  echo "Build with just build-dx, on Windows (vs2022-dx / CI), or copy a sample.exe in." >&2
  exit 1
fi

./scripts/dx-setup-wine.sh
bindir=$(dirname "$exe")
bindir=$(cd "$bindir" && pwd)
export WINEPREFIX="${WINEPREFIX:-$root/.wine-dx12}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d12core=n,b;d3d12=n,b;dxgi=n,b;mscoree,mshtml=}"
base=$(basename "$exe")
wine_run="$root/scripts/dx-wine.sh"
cd "$bindir"
exec "$wine_run" wine "./$base"
