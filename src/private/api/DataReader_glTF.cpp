#include "vuldir/DataReader.hpp"

#include <charconv>

using namespace vd;

static bool IsDataURI(Strv uri)
{
  return uri.starts_with("data:") && uri.find(";base64,") != Strv::npos;
}

static Arr<u8> DecodeDataURI(Strv uri)
{
  const char* dataStartTag = ";base64,";
  auto        dataStart    = uri.find(dataStartTag);
  if(dataStart == Strv::npos) return {};
  else
    return decodeBase64(uri.substr(dataStart + strlen(dataStartTag)));
}

bool DataReader::isGLTF(std::istream& src)
{
  const auto position = src.tellg();
  src >> std::ws;
  const bool result = src.peek() == '{';
  src.clear();
  src.seekg(position);
  return result;
}

bool DataReader::isBinaryGLTF(std::istream& src)
{
  const auto position = src.tellg();
  u32        magic    = 0u;
  src.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  const bool result =
    src.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
    magic == 0x46546c67u;
  src.clear();
  src.seekg(position);
  return result;
}

static Arr<data::Buffer> readBuffers(
  const Json::Array& src, DataReader::UriFilter& uriFilter,
  DataReader::FileReader&         fileReader,
  const DataReader::ModelOptions& options,
  Span<u8 const>                  binaryData = {})
{
  Arr<data::Buffer> ret;
  bool              usedBinaryData = false;

  for(const auto& info: src) {
    auto& buffer = ret.emplace_back();

    auto size         = info["byteLength"].AsNumber<u64>();
    buffer.byteLength = size;
    if(info["uri"].IsString()) {
      auto uri   = info["uri"].AsString();
      buffer.uri = uri;

      if(uriFilter && !uriFilter(uri)) { continue; }

      if(IsDataURI(uri)) {
        buffer.data = DecodeDataURI(uri);
        if(size != buffer.data.size())
          throw makeError<std::runtime_error>(
            "glTF: Invalid length for embedded buffer");
      } else {
        if(!fileReader)
          throw makeError<std::runtime_error>(
            "glTF: FileReader must be set if the asset has external "
            "URIs");

        buffer.data = fileReader(
          uri, options.basePath ? &*options.basePath : nullptr);
        if(size != buffer.data.size())
          throw makeError<std::runtime_error>(
            "glTF: Invalid length for buffer %s", Str(uri).c_str());
      }
    } else {
      if(usedBinaryData || size > binaryData.size())
        throw std::runtime_error("glTF: invalid binary buffer");
      buffer.data.assign(binaryData.begin(), binaryData.begin() + size);
      usedBinaryData = true;
    }
  }

  return ret;
}

static Arr<data::BufferView> readBufferViews(const Json::Array& src)
{
  Arr<data::BufferView> ret;

  for(const auto& info: src) {
    auto& view       = ret.emplace_back();
    view.bufferIndex = info["buffer"].AsNumber<u32>();
    view.length      = info["byteLength"].AsNumber<u64>();
    view.offset      = info["byteOffset"].AsNumberOpt<u64>(0u);
    view.stride      = info["byteStride"].AsNumberOpt<u32>(0u);
    view.target      = info["target"].AsNumberOpt<u32>();
  }

  return ret;
}

static Arr<data::Mesh> readMeshes(const Json::Array& src)
{
  Arr<data::Mesh> ret;

  for(const auto& meshInfo: src) {
    auto& mesh = ret.emplace_back();

    mesh.name = meshInfo["name"].AsStringOpt();

    for(const auto& primInfo: meshInfo["primitives"].AsArray()) {
      auto& prim = mesh.primitives.emplace_back();

      prim.indicesAccessorIndex =
        primInfo["indices"].AsNumberOpt<u32>();
      prim.material = primInfo["material"].AsNumberOpt<u32>();

      switch(primInfo["mode"].AsNumberOpt<u32>(4u)) {
        case 0u:
          prim.topology = PrimitiveTopology::PointList;
          break;
        case 1u:
          prim.topology = PrimitiveTopology::LineList;
          break;
        case 3u:
          prim.topology = PrimitiveTopology::LineStrip;
          break;
        case 4u:
          prim.topology = PrimitiveTopology::TriangleList;
          break;
        case 5u:
          prim.topology = PrimitiveTopology::TriangleStrip;
          break;
        case 2u:
          throw std::runtime_error(
            "glTF: line-loop topology is unsupported");
        case 6u:
          throw std::runtime_error(
            "glTF: triangle-fan topology is unsupported");
        default:
          throw std::runtime_error("glTF: invalid primitive topology");
      }

      for(const auto& attrInfo: primInfo["attributes"].AsObject()) {
        Opt<VertexAttribute> type;
        u32                  typeIndex = 0u;

        if(attrInfo.first == "POSITION")
          type = VertexAttribute::Position;
        else if(attrInfo.first == "NORMAL")
          type = VertexAttribute::Normal;
        else if(attrInfo.first == "TANGENT")
          type = VertexAttribute::Tangent;
        else {
          Strv prefix;
          if(attrInfo.first.starts_with("TEXCOORD_")) {
            type   = VertexAttribute::TexCoord;
            prefix = "TEXCOORD_";
          } else if(attrInfo.first.starts_with("COLOR_")) {
            type   = VertexAttribute::Color;
            prefix = "COLOR_";
          } else {
            // Skinning and custom attributes are not represented by the
            // public vertex-attribute enum, so omit them instead of
            // appending an uninitialized attribute.
            continue;
          }

          const char* first =
            attrInfo.first.data() + prefix.size();
          const char* last =
            attrInfo.first.data() + attrInfo.first.size();
          const auto parsed = std::from_chars(first, last, typeIndex);
          if(
            first == last || parsed.ec != std::errc{} ||
            parsed.ptr != last) {
            throw std::runtime_error(
              "glTF: invalid indexed vertex attribute semantic");
          }
        }

        prim.attributes.push_back(
          {.type          = *type,
           .typeIndex     = typeIndex,
           .accessorIndex = attrInfo.second.AsNumber<u32>()});
      }
    }
  }

  return ret;
}

static Arr<data::Accessor> readAccessors(const Json::Array& src)
{
  Arr<data::Accessor> ret;

  for(const auto& info: src) {
    if(info["sparse"].HasValue())
      throw std::runtime_error("glTF: sparse accessors are unsupported");

    auto& acc = ret.emplace_back();

    acc.bufferViewIndex = info["bufferView"].AsNumber<u32>();
    acc.count           = info["count"].AsNumber<u32>();
    acc.offset          = info["byteOffset"].AsNumberOpt<u64>(0u);
    if(info["normalized"].HasValue())
      acc.normalized = info["normalized"].AsBool();

    const auto& componentType = info["componentType"].AsNumber<u32>();
    switch(componentType) {
      case 5120u:
        acc.componentType = data::ComponentType::Byte;
        break;
      case 5121u:
        acc.componentType = data::ComponentType::UnsignedByte;
        break;
      case 5122u:
        acc.componentType = data::ComponentType::Short;
        break;
      case 5123u:
        acc.componentType = data::ComponentType::UnsignedShort;
        break;
      case 5125u:
        acc.componentType = data::ComponentType::UnsignedInt;
        break;
      case 5126u:
        acc.componentType = data::ComponentType::Float;
        break;
      default:
        throw std::runtime_error(
          "glTF: invalid accessor component type.");
    }

    const auto& type = info["type"].AsString();
    if(type == "SCALAR") {
      acc.type = data::AccessorType::Scalar;
    } else if(type == "VEC2") {
      acc.type = data::AccessorType::Vector2;
    } else if(type == "VEC3") {
      acc.type = data::AccessorType::Vector3;
    } else if(type == "VEC4") {
      acc.type = data::AccessorType::Vector4;
    } else if(type == "MAT2") {
      acc.type = data::AccessorType::Matrix22;
    } else if(type == "MAT3") {
      acc.type = data::AccessorType::Matrix33;
    } else if(type == "MAT4") {
      acc.type = data::AccessorType::Matrix44;
    } else {
      throw std::runtime_error("glTF: invalid accessor type.");
    }

    u32 boundComponentCount = 1u;
    switch(acc.type) {
      case data::AccessorType::Scalar:
        break;
      case data::AccessorType::Vector2:
        boundComponentCount = 2u;
        break;
      case data::AccessorType::Vector3:
        boundComponentCount = 3u;
        break;
      case data::AccessorType::Vector4:
      case data::AccessorType::Matrix22:
        boundComponentCount = 4u;
        break;
      case data::AccessorType::Matrix33:
        boundComponentCount = 9u;
        break;
      case data::AccessorType::Matrix44:
        boundComponentCount = 16u;
        break;
    }
    auto readBounds = [&](const Json::Value& value, const char* name) {
      Opt<Arr<f32>> result;
      if(!value.HasValue()) return result;
      if(!value.IsArray() || value.AsArray().size() != boundComponentCount)
        throw makeError<std::runtime_error>(
          "glTF: accessor %s has an invalid length", name);
      result.emplace();
      result->reserve(boundComponentCount);
      for(const auto& component: value.AsArray())
        result->push_back(component.AsNumber<f32>());
      return result;
    };
    acc.max = readBounds(info["max"], "max");
    acc.min = readBounds(info["min"], "min");

    if(
      acc.normalized &&
      (acc.componentType == data::ComponentType::UnsignedInt ||
       acc.componentType == data::ComponentType::Float))
      throw std::runtime_error(
        "glTF: accessor component type cannot be normalized");
  }

  return ret;
}

static void validateExtensions(const Json::Value& json)
{
  Arr<Strv> used;
  if(json["extensionsUsed"].HasValue()) {
    if(!json["extensionsUsed"].IsArray())
      throw std::runtime_error("glTF: extensionsUsed must be an array");
    for(const auto& value: json["extensionsUsed"].AsArray())
      used.emplace_back(value.AsString());
  }

  if(!json["extensionsRequired"].HasValue()) return;
  if(!json["extensionsRequired"].IsArray())
    throw std::runtime_error("glTF: extensionsRequired must be an array");

  static const Arr<Strv> supported{
    "KHR_materials_pbrSpecularGlossiness", "KHR_mesh_quantization"};
  for(const auto& value: json["extensionsRequired"].AsArray()) {
    const Strv extension = value.AsString();
    if(std::find(used.begin(), used.end(), extension) == used.end())
      throw std::runtime_error(
        "glTF: required extension is not listed in extensionsUsed");
    if(
      std::find(supported.begin(), supported.end(), extension) ==
      supported.end())
      throw makeError<std::runtime_error>(
        "glTF: required extension %.*s is unsupported",
        static_cast<int>(extension.size()), extension.data());
  }
}

static u64 getAccessorComponentSize(const data::Accessor& accessor)
{
  switch(accessor.componentType) {
    case data::ComponentType::Byte:
    case data::ComponentType::UnsignedByte:
      return 1u;
    case data::ComponentType::Short:
    case data::ComponentType::UnsignedShort:
      return 2u;
    case data::ComponentType::UnsignedInt:
    case data::ComponentType::Float:
      return 4u;
  }
  throw std::runtime_error("glTF: invalid accessor component type");
}

static u64 getAccessorElementSize(const data::Accessor& accessor)
{
  const u64 componentSize = getAccessorComponentSize(accessor);

  u64 rows = 1u;
  u64 columns = 1u;
  switch(accessor.type) {
    case data::AccessorType::Scalar:
      break;
    case data::AccessorType::Vector2:
      rows = 2u;
      break;
    case data::AccessorType::Vector3:
      rows = 3u;
      break;
    case data::AccessorType::Vector4:
      rows = 4u;
      break;
    case data::AccessorType::Matrix22:
      rows = columns = 2u;
      break;
    case data::AccessorType::Matrix33:
      rows = columns = 3u;
      break;
    case data::AccessorType::Matrix44:
      rows = columns = 4u;
      break;
  }

  // Matrix columns whose raw size is not 4-byte aligned are padded in glTF.
  const u64 columnSize = rows * componentSize;
  return columns == 1u
           ? columnSize
           : columns * alignUp(columnSize, static_cast<u64>(4u));
}

static void validateBufferViews(
  const Arr<data::BufferView>& views,
  const Arr<data::Buffer>&     buffers)
{
  for(const auto& view: views) {
    if(view.bufferIndex >= buffers.size())
      throw std::runtime_error("glTF: buffer view index out of range");
    if(view.length == 0u)
      throw std::runtime_error("glTF: buffer view length must be positive");

    const u64 bufferSize = buffers[view.bufferIndex].byteLength;
    if(view.offset > bufferSize || view.length > bufferSize - view.offset)
      throw std::runtime_error("glTF: buffer view data out of range");
    if(
      view.stride != 0u &&
      (view.stride < 4u || view.stride > 252u || view.stride % 4u != 0u))
      throw std::runtime_error("glTF: invalid buffer view byte stride");
    if(
      view.target && *view.target != 34962u && *view.target != 34963u)
      throw std::runtime_error("glTF: invalid buffer view target");
  }
}

static void validateAccessors(
  const Arr<data::Accessor>&   accessors,
  const Arr<data::BufferView>& bufferViews)
{
  for(const auto& accessor: accessors) {
    if(accessor.bufferViewIndex >= bufferViews.size())
      throw std::runtime_error("glTF: accessor buffer view out of range");
    if(accessor.count == 0u)
      throw std::runtime_error("glTF: accessor count must be positive");

    const auto& view = bufferViews[accessor.bufferViewIndex];
    const u64 componentSize = getAccessorComponentSize(accessor);
    const u64 elementSize = getAccessorElementSize(accessor);
    const u64 stride = view.stride == 0u ? elementSize : view.stride;
    if(
      accessor.offset % componentSize != 0u ||
      (view.offset + accessor.offset) % componentSize != 0u) {
      throw std::runtime_error("glTF: accessor offset is misaligned");
    }
    if(stride % componentSize != 0u)
      throw std::runtime_error("glTF: accessor stride is misaligned");
    if(stride < elementSize)
      throw std::runtime_error("glTF: accessor element exceeds byte stride");

    const u64 remainingElements = static_cast<u64>(accessor.count) - 1u;
    if(remainingElements > (MaxU64 - elementSize) / stride)
      throw std::runtime_error("glTF: accessor byte range overflow");
    const u64 byteLength = remainingElements * stride + elementSize;
    if(
      accessor.offset > view.length ||
      byteLength > view.length - accessor.offset)
      throw std::runtime_error("glTF: accessor data out of range");
  }
}

static Arr<data::Image> readImages(
  const Json::Array& src, DataReader& reader,
  DataReader::UriFilter&          uriFilter,
  const DataReader::ModelOptions& options,
  const Arr<data::Buffer>&        buffers,
  const Arr<data::BufferView>&    bufferViews)
{
  Arr<data::Image> ret;

  for(const auto& info: src) {
    if(info["uri"].IsString()) {
      auto uri = info["uri"].AsString();

      if(IsDataURI(uri)) {
        auto data = DecodeDataURI(uri);
        ret.emplace_back(reader.ReadImage(data, {}));
      } else {
        fs::path path;
        if(options.basePath) path = *options.basePath / uri;
        else
          path = uri;

        auto pathStr = pathToStr(path);

        if(uriFilter && !uriFilter(pathStr)) ret.emplace_back();
        else
          ret.emplace_back(reader.ReadImage(path, {}));
      }
    } else if(info["bufferView"].IsNumber()) {
      const u32 viewIndex = info["bufferView"].AsNumber<u32>();
      if(viewIndex >= bufferViews.size())
        throw std::runtime_error(
          "glTF: image buffer view out of range");
      const auto& view = bufferViews[viewIndex];
      if(view.bufferIndex >= buffers.size())
        throw std::runtime_error("glTF: image buffer out of range");
      const auto& buffer = buffers[view.bufferIndex].data;
      if(
        view.offset > buffer.size() ||
        view.length > buffer.size() - view.offset)
        throw std::runtime_error("glTF: image data out of range");
      ret.emplace_back(reader.ReadImage(
        Span<u8 const>{buffer.data() + view.offset, view.length}, {}));
    } else {
      throw std::runtime_error("glTF: image has no data source");
    }
  }

  return ret;
}

static Arr<data::Sampler> readSamplers(const Json::Array& src)
{
  Arr<data::Sampler> ret;

  for(const auto& info: src) {
    auto& sampler = ret.emplace_back();

    sampler.name = info["name"].AsStringOpt();

    const u32 magFilter = info["magFilter"].HasValue()
                            ? info["magFilter"].AsNumber<u32>()
                            : 9729u;
    switch(magFilter) {
      case 9728u:
        sampler.magFilter = SamplerFilter::Nearest;
        break;
      case 9729u:
        sampler.magFilter = SamplerFilter::Linear;
        break;
      default:
        throw std::runtime_error("glTF: invalid sampler magFilter");
    }

    const u32 minFilter = info["minFilter"].HasValue()
                            ? info["minFilter"].AsNumber<u32>()
                            : 9987u;
    switch(minFilter) {
      case 9728u:
        sampler.minFilter  = SamplerFilter::Nearest;
        sampler.mipFilter  = SamplerFilter::Nearest;
        sampler.useMipmaps = false;
        break;
      case 9729u:
        sampler.minFilter  = SamplerFilter::Linear;
        sampler.mipFilter  = SamplerFilter::Nearest;
        sampler.useMipmaps = false;
        break;
      case 9984u:
        sampler.minFilter  = SamplerFilter::Nearest;
        sampler.mipFilter  = SamplerFilter::Nearest;
        sampler.useMipmaps = true;
        break;
      case 9985u:
        sampler.minFilter  = SamplerFilter::Linear;
        sampler.mipFilter  = SamplerFilter::Nearest;
        sampler.useMipmaps = true;
        break;
      case 9986u:
        sampler.minFilter  = SamplerFilter::Nearest;
        sampler.mipFilter  = SamplerFilter::Linear;
        sampler.useMipmaps = true;
        break;
      case 9987u:
        sampler.minFilter  = SamplerFilter::Linear;
        sampler.mipFilter  = SamplerFilter::Linear;
        sampler.useMipmaps = true;
        break;
      default:
        throw std::runtime_error("glTF: invalid sampler minFilter");
    }

    const u32 wrapS = info["wrapS"].HasValue()
                        ? info["wrapS"].AsNumber<u32>()
                        : 10497u;
    switch(wrapS) {
      case 33071u:
        sampler.wrapU = SamplerAddressMode::Clamp;
        break;
      case 33648u:
        sampler.wrapU = SamplerAddressMode::Mirror;
        break;
      case 10497:
        sampler.wrapU = SamplerAddressMode::Repeat;
        break;
      default:
        throw std::runtime_error("glTF: invalid sampler wrapS");
    }

    const u32 wrapT = info["wrapT"].HasValue()
                        ? info["wrapT"].AsNumber<u32>()
                        : 10497u;
    switch(wrapT) {
      case 33071u:
        sampler.wrapV = SamplerAddressMode::Clamp;
        break;
      case 33648u:
        sampler.wrapV = SamplerAddressMode::Mirror;
        break;
      case 10497:
        sampler.wrapV = SamplerAddressMode::Repeat;
        break;
      default:
        throw std::runtime_error("glTF: invalid sampler wrapT");
    }
  }

  return ret;
}

static Arr<data::Texture> readTextures(const Json::Array& src)
{
  Arr<data::Texture> ret;

  for(const auto& info: src) {
    auto& texture = ret.emplace_back();

    texture.name         = info["name"].AsStringOpt();
    texture.imageIndex   = info["source"].AsNumberOpt<u32>();
    texture.samplerIndex = info["sampler"].AsNumberOpt<u32>();
  }

  return ret;
}

static Opt<data::TextureRef> readTextureRef(const Json::Value& info)
{
  if(!info.IsObject()) return std::nullopt;

  data::TextureRef ret;
  ret.textureIndex = info["index"].AsNumber<u32>();

  if(info["scale"].HasValue())
    ret.scale = info["scale"].AsNumber<f32>();
  else if(info["strength"].HasValue()) {
    ret.scale = info["strength"].AsNumber<f32>();
    if(*ret.scale < 0.f || *ret.scale > 1.f)
      throw std::runtime_error(
        "glTF: texture strength must be between 0 and 1");
  }

  ret.texCoord = info["texCoord"].HasValue()
                   ? info["texCoord"].AsNumber<u32>()
                   : 0u;

  return ret;
}

template<typename T>
static T readFixedVector(
  const Json::Value& value, const T& defaultValue, const char* name)
{
  if(!value.HasValue()) return defaultValue;
  if(!value.IsArray() || value.AsArray().size() != std::size(T{}))
    throw makeError<std::runtime_error>(
      "glTF: %s has an invalid length", name);
  return value.AsVector<T>();
}

static f32 readUnitFactor(
  const Json::Value& value, f32 defaultValue, const char* name)
{
  const f32 result =
    value.HasValue() ? value.AsNumber<f32>() : defaultValue;
  if(result < 0.f || result > 1.f)
    throw makeError<std::runtime_error>(
      "glTF: %s must be between 0 and 1", name);
  return result;
}

static Arr<data::Material> readMaterials(const Json::Array& src)
{
  Arr<data::Material> ret;

  for(const auto& matInfo: src) {
    auto& material = ret.emplace_back();

    material.name = matInfo["name"].AsStringOpt();

    {
      const auto& pbrInfo = matInfo["pbrMetallicRoughness"];
      if(pbrInfo.HasValue() && !pbrInfo.IsObject())
        throw std::runtime_error(
          "glTF: pbrMetallicRoughness must be an object");

      data::PbrMetallicRoughness param;
      param.baseColorFactor = readFixedVector(
        pbrInfo["baseColorFactor"], Float4{1, 1, 1, 1},
        "baseColorFactor");
      for(u32 idx = 0u; idx < param.baseColorFactor.size(); ++idx) {
        const auto component = param.baseColorFactor[idx];
        if(component < 0.f || component > 1.f)
          throw std::runtime_error(
            "glTF: baseColorFactor components must be between 0 and 1");
      }
      param.baseColorTexture =
        readTextureRef(pbrInfo["baseColorTexture"]);
      param.metallicFactor = readUnitFactor(
        pbrInfo["metallicFactor"], 1.f, "metallicFactor");
      param.roughnessFactor = readUnitFactor(
        pbrInfo["roughnessFactor"], 1.f, "roughnessFactor");
      param.metallicRoughnessTexture =
        readTextureRef(pbrInfo["metallicRoughnessTexture"]);

      material.model = param;
    }

    if(matInfo["extensions"]["KHR_materials_pbrSpecularGlossiness"]
         .IsObject()) {
      const auto& pbrInfo =
        matInfo["extensions"]["KHR_materials_pbrSpecularGlossiness"];

      data::PbrSpecularGlossiness param;
      param.diffuseFactor = readFixedVector(
        pbrInfo["diffuseFactor"], Float4{1, 1, 1, 1},
        "diffuseFactor");
      param.diffuseTexture = readTextureRef(pbrInfo["diffuseTexture"]);
      param.specularFactor = readFixedVector(
        pbrInfo["specularFactor"], Float3{1, 1, 1},
        "specularFactor");
      param.glossinessFactor = readUnitFactor(
        pbrInfo["glossinessFactor"], 1.f, "glossinessFactor");
      param.specularGlossinessTexture =
        readTextureRef(pbrInfo["specularGlossinessTexture"]);

      material.model = param;
    }

    material.normalTexture = readTextureRef(matInfo["normalTexture"]);
    material.occlusionTexture =
      readTextureRef(matInfo["occlusionTexture"]);
    material.emissiveTexture =
      readTextureRef(matInfo["emissiveTexture"]);
    material.emissiveFactor = readFixedVector(
      matInfo["emissiveFactor"], Float3{0, 0, 0},
      "emissiveFactor");
    for(u32 idx = 0u; idx < material.emissiveFactor.size(); ++idx) {
      const auto component = material.emissiveFactor[idx];
      if(component < 0.f)
        throw std::runtime_error(
          "glTF: emissiveFactor components must be non-negative");
    }

    material.alphaMode = AlphaMode::Opaque;
    material.alphaCutoff = matInfo["alphaCutoff"].HasValue()
                             ? matInfo["alphaCutoff"].AsNumber<f32>()
                             : 0.5f;
    if(matInfo["alphaMode"].HasValue()) {
      const auto mode = matInfo["alphaMode"].AsString();
      if(mode == "MASK")
        material.alphaMode = AlphaMode::Mask;
      else if(mode == "BLEND")
        material.alphaMode = AlphaMode::Blend;
      else if(mode != "OPAQUE")
        throw std::runtime_error("glTF: invalid material alphaMode");
    }

    material.doubleSided = matInfo["doubleSided"].HasValue()
                             ? matInfo["doubleSided"].AsBool()
                             : false;
  }

  return ret;
}

Arr<data::Node> readNodes(const Arr<Json::Value>& nodesInfo)
{
  Arr<data::Node> ret;
  ret.reserve(nodesInfo.size());

  for(const auto& nodeInfo: nodesInfo) {
    auto& node = ret.emplace_back();
    if(nodeInfo["name"].IsString())
      node.name = nodeInfo["name"].AsString();
    if(nodeInfo["mesh"].IsNumber())
      node.mesh = nodeInfo["mesh"].AsNumber<u32>();
    if(nodeInfo["children"].IsArray()) {
      for(const auto& c: nodeInfo["children"].AsArray()) {
        node.children.push_back(c.AsNumber<u32>());
      }
    }
    if(nodeInfo["matrix"].IsArray()) {
      const auto& arr = nodeInfo["matrix"].AsArray();
      if(arr.size() != 16u)
        throw std::runtime_error(
          "glTF: node matrix must contain 16 values");
      // glTF stores column-major; Float44 is row-major (m[row][col]).
      Float44 mat{};
      for(u32 i = 0; i < 16; ++i)
        mat[i % 4][i / 4] = arr[i].AsNumber<f32>();
      node.matrix = mat;
    }
    if(nodeInfo["translation"].IsArray()) {
      if(nodeInfo["translation"].AsArray().size() != 3u)
        throw std::runtime_error(
          "glTF: node translation must contain 3 values");
      node.translation =
        nodeInfo["translation"].AsVectorOpt<Float3>({0, 0, 0});
    }
    if(nodeInfo["rotation"].IsArray()) {
      if(nodeInfo["rotation"].AsArray().size() != 4u)
        throw std::runtime_error(
          "glTF: node rotation must contain 4 values");
      node.rotation =
        nodeInfo["rotation"].AsVectorOpt<Float4>({0, 0, 0, 1});
    }
    if(nodeInfo["scale"].IsArray()) {
      if(nodeInfo["scale"].AsArray().size() != 3u)
        throw std::runtime_error(
          "glTF: node scale must contain 3 values");
      node.scale = nodeInfo["scale"].AsVectorOpt<Float3>({1, 1, 1});
    }

    if(
      node.matrix &&
      (node.translation || node.rotation || node.scale)) {
      throw std::runtime_error(
        "glTF: node cannot define both matrix and TRS transforms");
    }
  }

  return ret;
}

static void validateTextureRef(
  const Opt<data::TextureRef>& ref, u64 textureCount)
{
  if(ref && ref->textureIndex >= textureCount)
    throw std::runtime_error("glTF: material texture index out of range");
}

static bool isUnsignedInteger(data::ComponentType type)
{
  return
    type == data::ComponentType::UnsignedByte ||
    type == data::ComponentType::UnsignedShort ||
    type == data::ComponentType::UnsignedInt;
}

static bool isInteger(data::ComponentType type)
{
  return type != data::ComponentType::Float;
}

static void validateAttributeAccessor(
  const data::Accessor& accessor, VertexAttribute semantic,
  bool meshQuantization)
{
  const auto type = accessor.componentType;
  switch(semantic) {
    case VertexAttribute::Position:
      if(accessor.type != data::AccessorType::Vector3)
        throw std::runtime_error("glTF: POSITION accessor must be VEC3");
      if(type == data::ComponentType::Float) return;
      if(meshQuantization && isInteger(type) && accessor.normalized) return;
      throw std::runtime_error(
        "glTF: POSITION accessor has an unsupported component type");
    case VertexAttribute::Normal:
      if(accessor.type != data::AccessorType::Vector3)
        throw std::runtime_error("glTF: NORMAL accessor must be VEC3");
      if(type == data::ComponentType::Float) return;
      if(
        accessor.normalized &&
        (type == data::ComponentType::Byte ||
         type == data::ComponentType::Short))
        return;
      if(meshQuantization && isInteger(type) && accessor.normalized) return;
      throw std::runtime_error(
        "glTF: NORMAL accessor has an unsupported component type");
    case VertexAttribute::Tangent:
      if(accessor.type != data::AccessorType::Vector4)
        throw std::runtime_error("glTF: TANGENT accessor must be VEC4");
      if(type == data::ComponentType::Float) return;
      if(
        accessor.normalized &&
        (type == data::ComponentType::Byte ||
         type == data::ComponentType::Short))
        return;
      if(meshQuantization && isInteger(type) && accessor.normalized) return;
      throw std::runtime_error(
        "glTF: TANGENT accessor has an unsupported component type");
    case VertexAttribute::TexCoord:
      if(accessor.type != data::AccessorType::Vector2)
        throw std::runtime_error("glTF: TEXCOORD accessor must be VEC2");
      if(type == data::ComponentType::Float) return;
      if(
        accessor.normalized &&
        (type == data::ComponentType::UnsignedByte ||
         type == data::ComponentType::UnsignedShort))
        return;
      if(meshQuantization && isInteger(type) && accessor.normalized) return;
      throw std::runtime_error(
        "glTF: TEXCOORD accessor has an unsupported component type");
    case VertexAttribute::Color:
      if(
        accessor.type != data::AccessorType::Vector3 &&
        accessor.type != data::AccessorType::Vector4)
        throw std::runtime_error("glTF: COLOR accessor must be VEC3 or VEC4");
      if(type == data::ComponentType::Float) return;
      if(
        accessor.normalized &&
        (type == data::ComponentType::UnsignedByte ||
         type == data::ComponentType::UnsignedShort))
        return;
      throw std::runtime_error(
        "glTF: COLOR accessor has an unsupported component type");
  }
}

static void validateModelReferences(
  const data::Model& model, bool meshQuantization)
{
  for(const auto& mesh: model.meshes) {
    for(const auto& primitive: mesh.primitives) {
      if(
        primitive.indicesAccessorIndex &&
        *primitive.indicesAccessorIndex >= model.accessors.size())
        throw std::runtime_error(
          "glTF: primitive index accessor out of range");
      if(primitive.material && *primitive.material >= model.materials.size())
        throw std::runtime_error("glTF: primitive material out of range");
      Opt<u32> vertexCount;
      bool     hasPosition = false;
      for(const auto& attribute: primitive.attributes) {
        if(attribute.accessorIndex >= model.accessors.size())
          throw std::runtime_error(
            "glTF: primitive attribute accessor out of range");
        const auto& accessor = model.accessors[attribute.accessorIndex];
        validateAttributeAccessor(
          accessor, attribute.type, meshQuantization);
        if(vertexCount && *vertexCount != accessor.count)
          throw std::runtime_error(
            "glTF: primitive attributes have inconsistent counts");
        vertexCount = accessor.count;
        if(attribute.type == VertexAttribute::Position) hasPosition = true;
        const auto& view = model.bufferViews[accessor.bufferViewIndex];
        if(view.target && *view.target != 34962u)
          throw std::runtime_error(
            "glTF: vertex accessor buffer view has an index target");
      }
      if(!hasPosition)
        throw std::runtime_error("glTF: primitive requires POSITION");
      if(primitive.indicesAccessorIndex) {
        const auto& accessor =
          model.accessors[*primitive.indicesAccessorIndex];
        if(
          accessor.type != data::AccessorType::Scalar ||
          !isUnsignedInteger(accessor.componentType) ||
          accessor.normalized)
          throw std::runtime_error("glTF: invalid primitive index accessor");
        const auto& view = model.bufferViews[accessor.bufferViewIndex];
        if(view.target && *view.target != 34963u)
          throw std::runtime_error(
            "glTF: index accessor buffer view has a vertex target");
      }
    }
  }

  for(const auto& texture: model.textures) {
    if(texture.imageIndex && *texture.imageIndex >= model.images.size())
      throw std::runtime_error("glTF: texture image index out of range");
    if(texture.samplerIndex && *texture.samplerIndex >= model.samplers.size())
      throw std::runtime_error("glTF: texture sampler index out of range");
  }

  for(const auto& material: model.materials) {
    if(const auto* pbr =
         std::get_if<data::PbrMetallicRoughness>(&material.model)) {
      validateTextureRef(pbr->baseColorTexture, model.textures.size());
      validateTextureRef(
        pbr->metallicRoughnessTexture, model.textures.size());
    } else if(const auto* pbr =
                std::get_if<data::PbrSpecularGlossiness>(
                  &material.model)) {
      validateTextureRef(pbr->diffuseTexture, model.textures.size());
      validateTextureRef(
        pbr->specularGlossinessTexture, model.textures.size());
    }
    validateTextureRef(material.normalTexture, model.textures.size());
    validateTextureRef(material.occlusionTexture, model.textures.size());
    validateTextureRef(material.emissiveTexture, model.textures.size());
  }

  Arr<u32> parentCounts(model.nodes.size(), 0u);
  for(const auto& node: model.nodes) {
    if(node.mesh && *node.mesh >= model.meshes.size())
      throw std::runtime_error("glTF: node mesh index out of range");
    for(const u32 child: node.children) {
      if(child >= model.nodes.size())
        throw std::runtime_error("glTF: child node index out of range");
      if(++parentCounts[child] > 1u)
        throw std::runtime_error("glTF: node has multiple parents");
    }
  }

  // Cycles are caught below, but a long enough acyclic chain still
  // overflows the stack here and again in any consumer that walks the
  // hierarchy. Deeper than any real scene graph.
  constexpr u32 maxNodeDepth = 256u;

  Arr<u8> visitState(model.nodes.size(), 0u);
  std::function<void(u32, u32)> visit = [&](u32 nodeIndex, u32 depth) {
    if(depth > maxNodeDepth)
      throw std::runtime_error("glTF: node hierarchy is too deep");
    if(visitState[nodeIndex] == 1u)
      throw std::runtime_error("glTF: node hierarchy contains a cycle");
    if(visitState[nodeIndex] == 2u) return;
    visitState[nodeIndex] = 1u;
    for(u32 child: model.nodes[nodeIndex].children)
      visit(child, depth + 1u);
    visitState[nodeIndex] = 2u;
  };
  for(u32 nodeIndex = 0u; nodeIndex < model.nodes.size(); ++nodeIndex)
    visit(nodeIndex, 0u);
}

static data::Model readGLTFJson(
  const Json::Value& json, DataReader& reader,
  DataReader::UriFilter& uriFilter, DataReader::FileReader& fileReader,
  const DataReader::ModelOptions& options,
  Span<u8 const>                  binaryData = {})
{
  if(
    !json["asset"].IsObject() ||
    json["asset"]["version"].AsStringOpt() != "2.0") {
    throw std::runtime_error("glTF: asset version must be 2.0");
  }
  validateExtensions(json);

  data::Model out;
  out.uri = options.uri.value_or("");

  if(json["buffers"].IsArray())
    out.buffers = readBuffers(
      json["buffers"].AsArray(), uriFilter, fileReader, options,
      binaryData);

  if(json["bufferViews"].IsArray())
    out.bufferViews = readBufferViews(json["bufferViews"].AsArray());
  validateBufferViews(out.bufferViews, out.buffers);

  if(json["meshes"].IsArray())
    out.meshes = readMeshes(json["meshes"].AsArray());

  if(json["accessors"].IsArray())
    out.accessors = readAccessors(json["accessors"].AsArray());
  validateAccessors(out.accessors, out.bufferViews);

  if(json["images"].IsArray())
    out.images = readImages(
      json["images"].AsArray(), reader, uriFilter, options, out.buffers,
      out.bufferViews);

  if(json["samplers"].IsArray())
    out.samplers = readSamplers(json["samplers"].AsArray());

  if(json["textures"].IsArray())
    out.textures = readTextures(json["textures"].AsArray());

  if(json["materials"].IsArray())
    out.materials = readMaterials(json["materials"].AsArray());

  if(json["nodes"].IsArray())
    out.nodes = readNodes(json["nodes"].AsArray());

  bool meshQuantization = false;
  if(json["extensionsUsed"].IsArray()) {
    for(const auto& value: json["extensionsUsed"].AsArray()) {
      if(value.AsString() == "KHR_mesh_quantization") {
        meshQuantization = true;
        break;
      }
    }
  }
  validateModelReferences(out, meshQuantization);

  if(json["scene"].IsNumber() && !json["scenes"].IsArray())
    throw std::runtime_error("glTF: default scene has no scenes array");

  if(json["scenes"].IsArray()) {
    const auto& scenes = json["scenes"].AsArray();
    if(scenes.empty()) {
      if(json["scene"].IsNumber())
        throw std::runtime_error("glTF: scene index out of range");
    } else {
      const u32 sceneIdx = json["scene"].AsNumberOpt<u32>(0u);
      if(sceneIdx >= scenes.size())
        throw std::runtime_error("glTF: scene index out of range");

      for(u32 idx = 0u; idx < scenes.size(); ++idx) {
        if(!scenes[idx]["nodes"].IsArray()) continue;
        for(const auto& node: scenes[idx]["nodes"].AsArray()) {
          const u32 nodeIndex = node.AsNumber<u32>();
          if(nodeIndex >= out.nodes.size())
            throw std::runtime_error(
              "glTF: scene node index out of range");
          if(idx == sceneIdx) out.rootNodes.push_back(nodeIndex);
        }
      }
    }
  }
  if(
    !json["scenes"].IsArray() && out.rootNodes.empty() &&
    !out.nodes.empty()) {
    Arr<bool> isChild(out.nodes.size(), false);
    for(const auto& node: out.nodes) {
      for(u32 child: node.children) {
        isChild[child] = true;
      }
    }
    for(u32 idx = 0u; idx < out.nodes.size(); ++idx) {
      if(!isChild[idx]) out.rootNodes.push_back(idx);
    }
  }

  return out;
}

data::Model
DataReader::readGLTF(std::istream& src, const ModelOptions& options)
{
  Json::Value json;
  json.Read(src);
  return readGLTFJson(json, *this, m_uriFilter, m_fileReader, options);
}

data::Model DataReader::readBinaryGLTF(
  std::istream& src, const ModelOptions& options)
{
  auto        bytes = streamReadBytes(src);
  ByteIStream input({bytes.data(), bytes.size()});

  const u32 magic   = input.Read<u32>();
  const u32 version = input.Read<u32>();
  const u32 length  = input.Read<u32>();
  if(
    magic != 0x46546c67u || version != 2u ||
    static_cast<u64>(length) != bytes.size())
    throw std::runtime_error("glTF: invalid binary header");

  Span<u8 const> jsonChunk;
  Span<u8 const> binaryChunk;
  while(input.HasMoreData()) {
    const u32 chunkLength = input.Read<u32>();
    const u32 chunkType   = input.Read<u32>();
    if(chunkLength % 4u != 0u)
      throw std::runtime_error("glTF: invalid binary chunk alignment");
    const auto chunk = input.ReadBytes(chunkLength);

    if(chunkType == 0x4e4f534au) {
      if(!jsonChunk.empty() || !binaryChunk.empty())
        throw std::runtime_error("glTF: invalid JSON chunk order");
      jsonChunk = chunk;
    } else if(chunkType == 0x004e4942u) {
      if(jsonChunk.empty() || !binaryChunk.empty())
        throw std::runtime_error("glTF: invalid binary chunk order");
      binaryChunk = chunk;
    }
  }
  if(jsonChunk.empty())
    throw std::runtime_error("glTF: missing JSON chunk");

  Str jsonText(
    reinterpret_cast<const char*>(jsonChunk.data()), jsonChunk.size());
  while(!jsonText.empty() && jsonText.back() == '\0')
    jsonText.pop_back();
  std::istringstream jsonStream(jsonText);
  Json::Value        json;
  json.Read(jsonStream);
  return readGLTFJson(
    json, *this, m_uriFilter, m_fileReader, options, binaryChunk);
}
