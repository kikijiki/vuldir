{
  description = "Vuldir development environment (Vulkan / Linux, DX12 via Wine)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      # Cross-compiled vkd3d-proton Windows DLLs for Wine, used to run Windows
      # DX12 builds on NixOS through the host Vulkan driver.
      vkd3dProtonWine64For =
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          cross = pkgs.pkgsCross.mingwW64;
          inherit (pkgs.vkd3d-proton.passthru.sources.vkd3d-proton) version src;
        in
        cross.stdenv.mkDerivation {
          pname = "vkd3d-proton-wine64";
          inherit version src;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            glslang
            wine64
          ];
          buildInputs = with cross; [
            windows.mcfgthreads
            windows.pthreads
          ];

          postPatch = ''
            substituteInPlace meson.build \
              --replace-fail "vkd3d_build = vcs_tag(" \
                             "vkd3d_build = vcs_tag( fallback : '$(cat .nixpkgs-auxfiles/vkd3d_build)'", \
              --replace-fail "vkd3d_version = vcs_tag(" \
                             "vkd3d_version = vcs_tag( fallback : '$(cat .nixpkgs-auxfiles/vkd3d_version)'",
          '';

          mesonBuildDir = "build";
          mesonFlags = [
            "--buildtype=release"
            "--cross-file"
            "build-win64.txt"
            "--prefix=${placeholder "out"}"
            "--bindir=x64"
            "--libdir=x64"
            "--strip"
          ];

          postInstall = ''
            cp $src/setup_vkd3d_proton.sh $out/
            chmod +x $out/setup_vkd3d_proton.sh
          '';
        };

      vulkanRuntimeEnv =
        pkgs:
        {
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath (
            with pkgs;
            [
              vulkan-loader
              vulkan-validation-layers
              vulkan-tools-lunarg
              libxcb
              wayland
            ]
          );
          VK_LAYER_PATH = pkgs.lib.concatStringsSep ":" [
            "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
            "${pkgs.vulkan-tools-lunarg}/share/vulkan/explicit_layer.d"
          ];
          XDG_DATA_DIRS = pkgs.lib.concatStringsSep ":" [
            "${pkgs.vulkan-validation-layers}/share"
            "${pkgs.vulkan-tools-lunarg}/share"
            "${pkgs.vulkan-tools}/share"
            "${pkgs.renderdoc}/share"
          ];
        };

      commonBuildPackages =
        pkgs: llvm:
        with pkgs;
        [
          cmake
          ninja
          ccache
          pkg-config
          just
          llvm.clang-tools
          llvm.lld
          directx-shader-compiler
          vulkan-headers
          vulkan-loader
          vulkan-validation-layers
          vulkan-tools
          vulkan-tools-lunarg
          vulkan-utility-libraries
          renderdoc
          libxcb
          libxcb-util
          libxcb-keysyms
          libxcb-wm
          wayland
          wayland-scanner
          wayland-protocols
          libxkbcommon
          dbus
          # RmlUi font engine
          freetype
        ];
    in
    {
      packages = forAllSystems (
        system:
        nixpkgs.lib.optionalAttrs (system == "x86_64-linux") {
          vkd3d-proton-wine64 = vkd3dProtonWine64For system;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          llvm = pkgs.llvmPackages_20;
          vulkanEnv = vulkanRuntimeEnv pkgs;
          dx12Supported = system == "x86_64-linux";
          vkd3dProtonWine64 = if dx12Supported then vkd3dProtonWine64For system else null;
          cross = pkgs.pkgsCross.mingwW64;
        in
        {
          default = pkgs.mkShell.override { stdenv = llvm.stdenv; } {
            packages = commonBuildPackages pkgs llvm;

            inherit (vulkanEnv) LD_LIBRARY_PATH VK_LAYER_PATH XDG_DATA_DIRS;

            shellHook = ''
              echo "vuldir nix shell: clang $(clang --version | head -n1)"
              echo "  just          # list recipes"
              echo "  just build-vk / just run-vk   # Vulkan (also: just build / just run)"
              echo "  just build-dx / just run-dx   # DX12 via Wine (auto-enters .#dx12)"
            '';
          };

          # Wine + vkd3d-proton shell for Windows DX12 builds.
          dx12 = pkgs.mkShell.override { stdenv = llvm.stdenv; } {
            packages =
              (if dx12Supported then
                builtins.filter
                  (package: package != pkgs.freetype)
                  (commonBuildPackages pkgs llvm)
               else
                commonBuildPackages pkgs llvm)
              ++ (with pkgs; [
                cabextract
                winetricks
              ])
              ++ pkgs.lib.optionals dx12Supported (
                with pkgs;
                [
                  wine64
                  vkd3dProtonWine64
                  dxvk
                  cross.stdenv.cc
                  cross.directx-headers
                  cross.freetype
                ]
              );

            inherit (vulkanEnv) LD_LIBRARY_PATH VK_LAYER_PATH XDG_DATA_DIRS;

            VKD3D_PROTON_DIR = if dx12Supported then "${vkd3dProtonWine64}" else "";
            VD_MINGW_FREETYPE_INCLUDE =
              if dx12Supported then "${pkgs.lib.getDev cross.freetype}/include/freetype2" else "";
            VD_MINGW_FREETYPE_LIBRARY =
              if dx12Supported then "${pkgs.lib.getLib cross.freetype}/lib/libfreetype.a" else "";
            VD_MINGW_FREETYPE_DEPS =
              if dx12Supported then
                "${pkgs.lib.getLib cross.libpng}/lib/libpng16.dll.a;${pkgs.lib.getLib cross.bzip2}/lib/libbz2.dll.a;${pkgs.lib.getLib cross.brotli}/lib/libbrotlidec.dll.a;${pkgs.lib.getLib cross.brotli}/lib/libbrotlicommon.dll.a;${pkgs.lib.getLib cross.zlib}/lib/libz.dll.a"
              else "";
            VD_MINGW_FREETYPE_DLLS =
              if dx12Supported then
                "${pkgs.lib.getLib cross.libpng}/bin/libpng16-16.dll;${pkgs.lib.getLib cross.bzip2}/bin/libbz2-1.dll;${cross.brotli}/bin/libbrotlidec.dll;${cross.brotli}/bin/libbrotlicommon.dll;${pkgs.lib.getLib cross.zlib}/bin/zlib1.dll"
              else "";
            # DXVK's dxgi.dll. Wine's dxgi with vkd3d-proton d3d12 crashes in
            # CreateSwapChainForHwnd.
            DXVK_DIR = if dx12Supported then "${pkgs.dxvk.bin}" else "";
            WINEDLLOVERRIDES = "d3d12core=n,b;d3d12=n,b;dxgi=n,b;mscoree,mshtml=";

            shellHook =
              if dx12Supported then
                ''
                  export WINEPREFIX="$PWD/.wine-dx12"
                  export VKD3D_PROTON_DIR DXVK_DIR WINEDLLOVERRIDES
                  export MCFGTHREAD_DLL="$(${toString pkgs.bash}/bin/bash -c 'matches=(/nix/store/*mcfgthread*/bin/libmcfgthread-2.dll); [[ -f "''${matches[0]:-}" ]] && echo "''${matches[0]}" || true')"
                  # The Wine prefix lives in the tree; sync DLLs on enter.
                  if [[ -x "$PWD/scripts/dx-setup-wine.sh" ]]; then
                    "$PWD/scripts/dx-setup-wine.sh" --quiet || true
                  fi
                  echo "vuldir dx12 shell: Wine $(wine --version 2>/dev/null | head -n1)"
                  echo "  mingw $(x86_64-w64-mingw32-g++ --version 2>/dev/null | head -n1)"
                  echo "  just build-dx / just run-dx"
                  echo "  (Wine prefix auto-setup; just *-dx also works from the default shell via nix develop .#dx12)"
                ''
              else
                ''
                  echo "vuldir dx12 shell is only supported on x86_64-linux (Wine/x64)."
                  echo "Use the default shell (nix develop) for Vulkan development."
                '';
          };
        }
      );
    };
}
