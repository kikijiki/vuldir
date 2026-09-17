# Development dependency pins

This file tracks pinned tool and library versions and why some pins are held back.

## Current pins (Linux Nix shell)

| Component | Version | Source |
|-----------|---------|--------|
| CMake | 4.3.4 | nixpkgs |
| Ninja | 1.13.2 | nixpkgs |
| LLVM/clang | 20.1.8 | `llvmPackages_20` |
| DXC | 1.10.2605.37 | nixpkgs `directx-shader-compiler` |
| Vulkan headers / loader / layers | 1.4.357.0 | nixpkgs |

## FetchContent fallbacks (non-Nix / Windows)

| Component | Pin | Notes |
|-----------|-----|-------|
| Vulkan-Headers | `vulkan-sdk-1.4.357.0` | Matches Nix shell when `VD_USE_VULKAN_SDK=OFF` |
| Vulkan-ValidationLayers | `vulkan-sdk-1.4.357.0` | Kept in sync with headers |
| DXC | v1.9.2602 (`dxc_2026_02_20`) | Production release; Nix shell uses newer 1.10 |

## CI tool versions (`.github/workflows/build.yml`)

| Component | Version | Notes |
|-----------|---------|-------|
| CMake (Windows) | 4.3.4 | Matches Nix shell |
| Ninja (Windows) | 1.13.2 | Matches Nix shell |
| LLVM (Windows/Linux) | 20.1.8 | Matches Nix shell |
| Vulkan SDK (Windows) | 1.4.309.0 | **Held back**: Lunarg CDN has no Windows installer for 1.4.357.0 yet |
| Vulkan SDK (Linux) | apt `vulkan-sdk` | Uses Lunarg Ubuntu repo (tracks distro, not pinned) |

## Deliberately not bumped

- **CI Windows Vulkan SDK (1.4.309.0):** `https://sdk.lunarg.com/sdk/download/1.4.357.0/...` returns 404; keep 1.4.309.0 until Lunarg publishes the installer.
- **FetchContent DXC to 1.10.x:** 1.10 is a Shader Model 6.10 preview; stay on production 1.9.2602 for Windows/off-Nix fallback downloads.
- **LLVM 21 / `llvmPackages_21`:** Stay on LLVM 20 to avoid toolchain churn; bump when the project moves to C++23 or needs newer clang-tidy checks.
- **cmake_minimum_required 4.x:** Presets and Nix already use CMake 4.x, but minimum stays at 3.23 so non-Nix environments on older distros can still configure.
