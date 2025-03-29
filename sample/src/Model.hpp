#pragma once

#include "vuldir/api/Api.hpp"
#include "vuldir/core/Core.hpp"

namespace vd {

class Model
{
public:
  struct MeshPrimitive {
    struct Attribute {
      VertexAttribute type;
      u32             typeIndex;
      u32             buffer;
    };

    PrimitiveTopology topology;
    Arr<Attribute>    attributes;
    Opt<u32>          indices;
    Opt<u32>          material;
  };

  struct Texture {
    u32 image;
    u32 sampler;
    u32 texCoord = 0u;
    f32 scale    = 1.0f;
  };

  struct PbrMetallicRoughness {
    Float4       baseColorFactor;
    Opt<Texture> baseColorTexture;

    f32          metallicFactor;
    f32          roughnessFactor;
    Opt<Texture> metallicRoughnessTexture;
  };

  struct PbrSpecularGlossiness {
    Float4       diffuseFactor;
    Opt<Texture> diffuseTexture;

    Float3       specularFactor;
    f32          glossinessFactor;
    Opt<Texture> specularGlossinessTexture;
  };

  struct Material {
    Opt<Str> name;

    Var<std::monostate, PbrMetallicRoughness, PbrSpecularGlossiness>
      model;

    Opt<TextureRef> normalTexture;
    Opt<TextureRef> occlusionTexture;
    Opt<TextureRef> emissiveTexture;
    Float3          emissiveFactor;

    AlphaMode alphaMode;
    f32       alphaCutoff;

    bool doubleSided;
  };

private:
  Arr<Buffer>        m_buffers;
  Arr<Image>         m_images;
  Arr<MeshPrimitive> m_primitives;
  Arr<Material>      m_materials;
};

} // namespace vd
