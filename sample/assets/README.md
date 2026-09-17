# Sample showcase assets

Default PBR/IBL demo content for the Vuldir sample. Avocado is kept for
regression / drag-drop testing; DamagedHelmet is the default model.

## Layout

| Path | Role |
|------|------|
| `DamagedHelmet/glTF/DamagedHelmet.gltf` | Default showcase model (PBR metal/paint/emissive) |
| `env/studio_small_09_1k.hdr` | Studio IBL (Radiance HDR, 1k) |
| `Avocado/` | Original tiny glTF sample (unchanged) |

Re-fetch / regenerate with `./download_showcase.sh` from this directory.

## DamagedHelmet

- **Source:** [KhronosGroup/glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets), `Models/DamagedHelmet`
- **Upstream:** https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet
- **License:** CC-BY-4.0 (see `DamagedHelmet/LICENSE.md`; also dual-listed CC-BY-NC-4.0 upstream)
- **Author:** theblueturtle_ (via Khronos sample assets)
- **Format note:** Upstream ships JPEG textures. This tree stores **PNG** copies of the same maps because Vuldir's `DataReader` currently loads PNG only. Geometry/bin and material wiring match the plain `glTF/` variant (no Draco).

## IBL: studio_small_09 (1k)

- **Source:** [Poly Haven: Studio Small 09](https://polyhaven.com/a/studio_small_09)
- **Download:** https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_09_1k.hdr
- **License:** [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)
- **Author:** Sergej Majboroda
- **Format:** Radiance `.hdr`, 1k equirectangular (~1.6 MB)

For a sharper env, use the 2k HDR from the same Poly Haven page (~6 MB) or run
`./download_showcase.sh --hdr-2k`.

## Scale

`Scene::loadGltfModel` auto-fits the model AABB so the largest axis is 1.5
units (orbit camera radius is 2). No per-asset scale constant is required.

## Approximate sizes (committed)

| Asset | Size |
|-------|------|
| DamagedHelmet (PNG textures + bin) | ~15 MB |
| `studio_small_09_1k.hdr` | ~1.6 MB |
| **Total new** | **~17 MB** |
