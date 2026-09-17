# API is a postfix, e.g. just build-vk / just run-dx.
# Vulkan uses the default nix shell. The *-dx recipes re-enter nix develop
# .#dx12 via scripts/dx-ensure-env.sh when Wine/vkd3d/DXVK are missing.

set shell := ["bash", "-euo", "pipefail", "-c"]

config := "debug"
# SSH shells often omit display vars.
export DISPLAY := env_var_or_default("DISPLAY", ":0")
export WAYLAND_DISPLAY := env_var_or_default("WAYLAND_DISPLAY", "wayland-1")

# List recipes
default:
    @just --list

# Configure the Vulkan ninja build
configure-vk:
    cmake --preset ninja-vk

# Build Vulkan (config: debug|release|asan)
build-vk config=config: configure-vk
    cmake --build --preset ninja-vk-{{config}}

# Rebuild Vulkan without reconfigure
rebuild-vk config=config:
    cmake --build --preset ninja-vk-{{config}}

# Build and run Catch2 unit + integration tests
test-vk config=config: (build-vk config)
    cd _build/ninja-vk && ctest -C {{ if config == "asan" { "ASAN" } else { titlecase(config) } }} --output-on-failure --timeout 120

# Run Vulkan tests with the requested headless Linux display server.
ci-test-vk window config=config:
    ./scripts/ci-test-vk.sh "{{window}}" "{{config}}"

# Run the Vulkan sample (cwd = binary dir so Shaders/ resolve)
run-vk config=config: (build-vk config)
    cd _build/ninja-vk/sample/{{ if config == "asan" { "ASAN" } else { titlecase(config) } }} && ./{{ if config == "debug" { "sample-d" } else { "sample" } }}

# Aliases for Vulkan
configure: configure-vk
build config=config: (build-vk config)
rebuild config=config: (rebuild-vk config)
test config=config: (test-vk config)
run config=config: (run-vk config)

# Verify wine, vulkaninfo, vkd3d-proton, and DXVK dxgi
smoke-dx:
    ./scripts/dx-ensure-env.sh ./scripts/dx-smoke.sh

# Ensure .wine-dx12 has vkd3d-proton d3d12 + DXVK dxgi (idempotent)
setup-dx:
    ./scripts/dx-ensure-env.sh ./scripts/dx-setup-wine.sh

# Cross-compile the DX12 sample for Windows (mingw-w64)
build-dx config="debug":
    ./scripts/dx-ensure-env.sh ./scripts/dx-build.sh {{config}}

# Run the mingw-built sample.exe under Wine
# Does not depend on setup-dx: the dependency would re-enter .#dx12 only for
# setup, then resume this recipe without wine on PATH.
run-dx config="debug":
    ./scripts/dx-ensure-env.sh ./scripts/dx-run.sh {{config}}

# Run an arbitrary Windows-built sample.exe under Wine
run-exe-dx exe:
    ./scripts/dx-ensure-env.sh ./scripts/dx-run-exe.sh {{exe}}

# Clean build tree
clean:
    rm -rf _build

# Deep clean (build + cached DXC downloads)
distclean: clean
    rm -rf _cache

# Show Vulkan devices / layers
vulkaninfo *args:
    vulkaninfo {{args}}

# Open RenderDoc GUI
renderdoc:
    qrenderdoc
