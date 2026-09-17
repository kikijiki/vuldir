#!/usr/bin/env bash
# Re-download showcase assets into sample/assets/.
# Safe to re-run; overwrites DamagedHelmet glTF + env HDR.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HDR_RES=1k
CONVERT_PNG=1

usage() {
  cat <<EOF
Usage: $(basename "$0") [--hdr-1k|--hdr-2k] [--keep-jpg]

Downloads:
  - Khronos DamagedHelmet (plain glTF, no Draco)
  - Poly Haven studio_small_09 HDR (CC0)

By default JPEG textures are converted to PNG for Vuldir's PNG-only loader
(requires ImageMagick: magick or convert).
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --hdr-1k) HDR_RES=1k ;;
    --hdr-2k) HDR_RES=2k ;;
    --keep-jpg) CONVERT_PNG=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
  shift
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1" >&2; exit 1; }; }
need curl

GLTF_BASE="https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet"
HDR_URL="https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/${HDR_RES}/studio_small_09_${HDR_RES}.hdr"

mkdir -p "$ROOT/DamagedHelmet/glTF" "$ROOT/env"
cd "$ROOT/DamagedHelmet/glTF"

echo "==> DamagedHelmet glTF"
for f in DamagedHelmet.gltf DamagedHelmet.bin \
         Default_AO.jpg Default_albedo.jpg Default_emissive.jpg \
         Default_metalRoughness.jpg Default_normal.jpg; do
  echo "  $f"
  curl -fsSL -o "$f" "$GLTF_BASE/glTF/$f"
done
curl -fsSL -o "$ROOT/DamagedHelmet/LICENSE.md" "$GLTF_BASE/LICENSE.md"
curl -fsSL -o "$ROOT/DamagedHelmet/README.md" "$GLTF_BASE/README.md"

if [[ "$CONVERT_PNG" -eq 1 ]]; then
  if command -v magick >/dev/null 2>&1; then
    IM=(magick)
  elif command -v convert >/dev/null 2>&1; then
    IM=(convert)
  else
    echo "ImageMagick not found; leaving JPEGs (loader may fail). Install magick or use --keep-jpg." >&2
    IM=()
  fi
  if [[ ${#IM[@]} -gt 0 ]]; then
    echo "==> Converting JPEG textures -> PNG"
    for f in *.jpg; do
      out="${f%.jpg}.png"
      "${IM[@]}" "$f" -define png:compression-level=9 "$out"
      rm -f "$f"
    done
    # portable sed for glTF uri swap
    tmp="$(mktemp)"
    sed 's/\.jpg/.png/g' DamagedHelmet.gltf >"$tmp"
    mv "$tmp" DamagedHelmet.gltf
  fi
fi

echo "==> HDR studio_small_09_${HDR_RES}"
curl -fsSL -o "$ROOT/env/studio_small_09_${HDR_RES}.hdr" "$HDR_URL"
# Keep the sample default filename stable when fetching 1k
if [[ "$HDR_RES" == "1k" ]]; then
  :
elif [[ ! -e "$ROOT/env/studio_small_09_1k.hdr" ]]; then
  echo "Note: main.cpp defaults to studio_small_09_1k.hdr; you fetched ${HDR_RES}."
fi

echo "==> Done"
du -sh "$ROOT/DamagedHelmet" "$ROOT/env" || true
