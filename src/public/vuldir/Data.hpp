#pragma once

#include "vuldir/api/Api.hpp"
#include "vuldir/core/Core.hpp"

namespace vd::data {

struct Image {
  Opt<Str> uri;
  Format   format;
  UInt2    size;
  Arr<u8>  texels;
};

enum class ComponentType {
  Byte,
  UnsignedByte,
  Short,
  UnsignedShort,
  UnsignedInt,
  Float
};

enum class AccessorType {
  Scalar,
  Vector2,
  Vector3,
  Vector4,
  Matrix22,
  Matrix33,
  Matrix44
};

struct Buffer {
  Opt<Str> uri;
  Arr<u8>  data;
};

struct BufferView {
  u32      bufferIndex;
  u64      offset;
  u64      length;
  u64      stride;
  Opt<u32> target;
};

struct Accessor {
  u32           bufferViewIndex;
  u64           offset;
  u32           count;
  ComponentType componentType;
  AccessorType  type;
  Opt<Float4>   min;
  Opt<Float4>   max;
};

struct MeshPrimitive {
  struct Attribute {
    VertexAttribute type;
    u32
      typeIndex; // For attributes that can have multiple sets of value, like TEXCOORD_0,1,2...
    u32 accessorIndex;
  };
  PrimitiveTopology topology;
  Opt<u32>          indicesAccessorIndex;
  Opt<u32>          material;
  Arr<Attribute>    attributes;
};

struct Texture {
  Opt<Str> name;
  Opt<u32> imageIndex;
  Opt<u32> samplerIndex;
};

struct TextureRef {
  u32      textureIndex;
  u32      texCoord;
  Opt<f32> scale;
};

struct Sampler {
  Opt<Str>           name;
  SamplerFilter      magFilter;
  SamplerFilter      minFilter;
  SamplerFilter      mipFilter;
  SamplerAddressMode wrapU;
  SamplerAddressMode wrapV;
  bool               useMipmaps;
};

struct PbrMetallicRoughness {
  Float4          baseColorFactor;
  Opt<TextureRef> baseColorTexture;

  f32             metallicFactor;
  f32             roughnessFactor;
  Opt<TextureRef> metallicRoughnessTexture;
};

struct PbrSpecularGlossiness {
  Float4          diffuseFactor;
  Opt<TextureRef> diffuseTexture;

  Float3          specularFactor;
  f32             glossinessFactor;
  Opt<TextureRef> specularGlossinessTexture;
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

struct Mesh {
  Opt<Str>           name;
  Arr<MeshPrimitive> primitives;
};

struct Model {
  Str             uri;
  Arr<Buffer>     buffers;
  Arr<BufferView> bufferViews;
  Arr<Accessor>   accessors;
  Arr<Mesh>       meshes;
  Arr<Image>      images;
  Arr<Sampler>    samplers;
  Arr<Texture>    textures;
  Arr<Material>   materials;
};

} // namespace vd::data
