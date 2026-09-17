#!/usr/bin/env bash
set -euo pipefail

window_system="${1:?usage: ci-test-vk.sh <wayland|xcb> <debug|release|asan>}"
configuration="${2:?usage: ci-test-vk.sh <wayland|xcb> <debug|release|asan>}"

case "$configuration" in
  debug) config_name=Debug ;;
  release) config_name=Release ;;
  asan) config_name=ASAN ;;
  *)
    echo "unsupported configuration: $configuration" >&2
    exit 2
    ;;
esac

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ctest_args=(
  --test-dir "$repo_root/_build/ninja-vk"
  -C "$config_name"
  --output-on-failure
  --timeout 120
)

if [[ -z "${VK_DRIVER_FILES:-}" || ! -f "$VK_DRIVER_FILES" ]]; then
  shopt -s nullglob
  lvp_icds=(
    /usr/share/vulkan/icd.d/lvp_icd*.json
    /run/opengl-driver/share/vulkan/icd.d/lvp_icd*.json
  )
  if (( ${#lvp_icds[@]} == 0 )); then
    echo "lavapipe ICD manifest not found" >&2
    exit 1
  fi
  export VK_DRIVER_FILES="${lvp_icds[0]}"
fi

export VD_TEST_REQUIRE_GPU=1
export LIBGL_ALWAYS_SOFTWARE=1

case "$window_system" in
  xcb)
    command -v xvfb-run >/dev/null
    exec xvfb-run -a ctest "${ctest_args[@]}"
    ;;
  wayland)
    command -v weston >/dev/null
    runtime_parent="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
    runtime_dir=$(mktemp -d "$runtime_parent/vuldir-wayland.XXXXXX")
    chmod 700 "$runtime_dir"
    export XDG_RUNTIME_DIR="$runtime_dir"
    export WAYLAND_DISPLAY=wayland-ci
    weston_log="$runtime_dir/weston.log"

    weston \
      --backend=headless-backend.so \
      --use-pixman \
      --socket="$WAYLAND_DISPLAY" \
      --idle-time=0 \
      --log="$weston_log" &
    weston_pid=$!

    cleanup()
    {
      kill "$weston_pid" 2>/dev/null || true
      wait "$weston_pid" 2>/dev/null || true
    }
    trap cleanup EXIT

    for _ in $(seq 1 50); do
      [[ -S "$runtime_dir/$WAYLAND_DISPLAY" ]] && break
      if ! kill -0 "$weston_pid" 2>/dev/null; then
        cat "$weston_log" >&2
        exit 1
      fi
      sleep 0.1
    done

    if [[ ! -S "$runtime_dir/$WAYLAND_DISPLAY" ]]; then
      cat "$weston_log" >&2
      exit 1
    fi
    ctest "${ctest_args[@]}"
    ;;
  *)
    echo "unsupported window system: $window_system" >&2
    exit 2
    ;;
esac
