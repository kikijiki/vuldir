#!/usr/bin/env bash
# Run a Wine command without development-shell loader overrides.
set -euo pipefail

if [[ $# -eq 0 ]]; then
  echo "usage: dx-wine.sh <wine-command> [args...]" >&2
  exit 2
fi

# Wine's preloader can crash in glibc when the environment is large (Nix
# compiler flags, direnv's DIRENV_DIFF), so pass an allowlist. nixpkgs' Wine
# carries its own runtime paths.
command_name="$1"
shift

if [[ "$command_name" == */* ]]; then
  command_path="$command_name"
else
  command_path="$(command -v "$command_name" || true)"
fi

if [[ -z "$command_path" || ! -x "$command_path" ]]; then
  echo "Wine command not found: $command_name" >&2
  exit 127
fi

wine_bin_dir="$(cd "$(dirname "$command_path")" && pwd)"
clean_env=(
  env -i
  "HOME=$HOME"
  "PATH=$wine_bin_dir:/run/current-system/sw/bin:/usr/bin:/bin"
)

for name in USER LOGNAME SHELL LANG LC_ALL LC_CTYPE TZ DISPLAY WAYLAND_DISPLAY \
  XAUTHORITY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS XDG_DATA_DIRS \
  XDG_CONFIG_DIRS; do
  if [[ -v "$name" ]]; then
    clean_env+=("$name=${!name}")
  fi
done

# Keep Wine and graphics runtime variables.
for name in ${!WINE@} ${!DXVK@} ${!VKD3D@} ${!VK_@} ${!MESA_@}; do
  clean_env+=("$name=${!name}")
done
if [[ -v DRI_PRIME ]]; then
  clean_env+=("DRI_PRIME=$DRI_PRIME")
fi

exec "${clean_env[@]}" "$command_path" "$@"
