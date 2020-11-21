#include "vuldir/DataReader.hpp"

#include <charconv>

using namespace vd;

// static bool IsDataURI(Strv uri)
//{
//  return uri.starts_with("data:") && (uri.find_first_of(";base64,") !=
//  Strv::npos);
//}
//
// static Arr<u8> DecodeDataURI(Strv uri)
//{
//  const char* dataStartTag = ";base64,";
//  auto        dataStart    = uri.find_first_of(dataStartTag);
//  if(dataStart == Strv::npos)
//    return {};
//  else
//    return decodeBase64(uri.substr(dataStart + strlen(dataStartTag)));
//}
//
// bool DataReader::isGLTF(std::istream& src)
//{
//  // TODO
//  return src.peek() != 0x46546C67;
//}
//
// bool DataReader::isBinaryGLTF(std::istream& src)
//{
//  return src.peek() == 0x46546C67;
//}
//
// data::Model DataReader::readBinaryGLTF(std::istream& src, const
// ModelOptions& options)
//{
//  if(!isGLTF(src))
//    throw std::runtime_error("glTF: Bad signature");
//
//  data::Model model;
//  // TODO
//  GG_UNUSED(options);
//  return model;
//}
//
// static Arr<data::Buffer> readBuffers(const Json::Array& src,
// DataReader::UriFilter& uriFilter, DataReader::FileReader& fileReader,
// const DataReader::ModelOptions& options)
//{
//  Arr<data::Buffer> ret;
//
//  for(const auto& info: src) {
//    auto& buffer = ret.emplace_back();
//
//    auto size = info["byteLength"].AsNumber<u64>();
//    if(info["uri"].IsString()) {
//      auto uri   = info["uri"].AsString();
//      buffer.uri = uri;
//
//      if(uriFilter && !uriFilter(uri)) {
//        ret.emplace_back();
//        continue;
//      }
//
//      if(IsDataURI(uri)) {
//        buffer.data = DecodeDataURI(uri);
//        if(size != buffer.data.size())
//          throw makeError<std::runtime_error>(
//            "glTF: Invalid length for embedded buffer");
//      } else {
//        if(!fileReader)
//          throw makeError<std::runtime_error>(
//            "glTF: FileReader must be set if the asset has external
//            URIs");
//
//        buffer.data = fileReader(uri, options.basePath ?
//        &*options.basePath : nullptr); if(size != buffer.data.size())
//          throw makeError<std::runtime_error>(
//            "glTF: Invalid length for buffer %s", Str(uri).c_str());
//      }
//    } else {
//      // TODO
//      throw makeError<std::runtime_error>(
//        "glTF: Binary format not implemented yet.");
//    }
//  }
//
//  return ret;
//}
//
// static Arr<data::BufferView> readBufferViews(const Json::Array& src)
//{
//  Arr<data::BufferView> ret;
//
//  for(const auto& info: src) {
//    auto& view       = ret.emplace_back();
//    view.bufferIndex = info["buffer"].AsNumber<u32>();
//    view.length      = info["byteLength"].AsNumber<u32>();
//    view.offset      = info["byteOffset"].AsNumberOpt<u32>(0u);
//    view.stride      = info["byteStride"].AsNumberOpt<u32>(0u);
//    view.target      = info["target"].AsNumberOpt<u32>();
//  }
//
//  return ret;
//}
//
// static Arr<data::Mesh> readMeshes(const Json::Array& src)
//{
//  Arr<data::Mesh> ret;
//
//  for(const auto& meshInfo: src) {
//    auto& mesh = ret.emplace_back();
//
//    mesh.name = meshInfo["name"].AsStringOpt();
//
//    for(const auto& primInfo: meshInfo["primitives"].AsArray()) {
//      auto& prim = mesh.primitives.emplace_back();
//
//      prim.indicesAccessorIndex =
//      primInfo["indices"].AsNumberOpt<u32>(); prim.material =
//      primInfo["material"].AsNumberOpt<u32>();
//
//      for(const auto& attrInfo: primInfo["attributes"].AsObject()) {
//        auto& attr         = prim.attributes.emplace_back();
//        attr.accessorIndex = attrInfo.second.AsNumber<u32>();
//
//        if(attrInfo.first == "POSITION")
//          attr.type = VertexAttribute::Position;
//        else if(attrInfo.first == "NORMAL")
//          attr.type = VertexAttribute::Normal;
//        else if(attrInfo.first == "TANGENT")
//          attr.type = VertexAttribute::Tangent;
//        else if(attrInfo.first.starts_with("TEXCOORD_")) {
//          attr.type = VertexAttribute::TexCoord;
//          std::from_chars(
//            attrInfo.first.data() + strlen("TEXCOORD_"),
//            attrInfo.first.data() + attrInfo.first.size(),
//            attr.typeIndex);
//        } else if(attrInfo.first.starts_with("COLOR_")) {
//          attr.type = VertexAttribute::Color;
//          std::from_chars(
//            attrInfo.first.data() + strlen("COLOR_"),
//            attrInfo.first.data() + attrInfo.first.size(),
//            attr.typeIndex);
//        } else if(attrInfo.first.starts_with("JOINTS")) {
//          // TODO
//        } else if(attrInfo.first.starts_with("WEIGHTS")) {
//          // TODO
//        }
//      }
//    }
//  }
//
//  return ret;
//}
//
// static Arr<data::Accessor> readAccessors(const Json::Array& src)
//{
//  Arr<data::Accessor> ret;
//
//  for(const auto& info: src) {
//    auto& acc = ret.emplace_back();
//
//    acc.bufferViewIndex = info["bufferView"].AsNumber<u32>();
//    acc.count           = info["count"].AsNumber<u32>();
//    acc.offset          = info["offset"].AsNumberOpt<u32>(0u);
//
//    acc.max = info["max"].AsVectorOpt<Float4>();
//    acc.min = info["max"].AsVectorOpt<Float4>();
//
//    const auto& componentType = info["componentType"].AsNumber<u32>();
//    switch(componentType) {
//      case 5120u:
//        acc.componentType = data::ComponentType::Byte;
//        break;
//      case 5121u:
//        acc.componentType = data::ComponentType::UnsignedByte;
//        break;
//      case 5122u:
//        acc.componentType = data::ComponentType::Short;
//        break;
//      case 5123u:
//        acc.componentType = data::ComponentType::UnsignedShort;
//        break;
//
//      case 5125u:
//        acc.componentType = data::ComponentType::UnsignedInt;
//        break;
//      case 5126u:
//        acc.componentType = data::ComponentType::Float;
//        break;
//      default:
//        throw std::runtime_error("glTF: invalid accessor component
//        type.");
//    }
//
//    const auto& type = info["type"].AsString();
//    if(type == "SCALAR") {
//      acc.type = data::AccessorType::Scalar;
//    } else if(type == "VEC2") {
//      acc.type = data::AccessorType::Vector2;
//    } else if(type == "VEC3") {
//      acc.type = data::AccessorType::Vector3;
//    } else if(type == "VEC4") {
//      acc.type = data::AccessorType::Vector4;
//    } else if(type == "MAT2") {
//      acc.type = data::AccessorType::Matrix22;
//    } else if(type == "MAT3") {
//      acc.type = data::AccessorType::Matrix33;
//    } else if(type == "MAT4") {
//      acc.type = data::AccessorType::Matrix44;
//    } else {
//      throw std::runtime_error("glTF: invalid accessor type.");
//    }
//  }
//
//  return ret;
//}
//
// static Arr<data::Image> readImages(const Json::Array& src,
// DataReader& reader, DataReader::UriFilter& uriFilter, const
// DataReader::ModelOptions& options)
//{
//  Arr<data::Image> ret;
//
//  for(const auto& info: src) {
//    if(info["uri"].IsString()) {
//      auto uri = info["uri"].AsString();
//
//      if(IsDataURI(uri)) {
//        auto data = DecodeDataURI(uri);
//        ret.emplace_back(reader.ReadImage(data, {}));
//      } else {
//        fs::path path;
//        if(options.basePath)
//          path = *options.basePath / uri;
//        else
//          path = uri;
//
//        auto pathStr = pathToStr(path);
//
//        if(uriFilter && !uriFilter(pathStr))
//          ret.emplace_back();
//        else
//          ret.emplace_back(reader.ReadImage(path, {}));
//      }
//    }
//  }
//
//  return ret;
//}
//
// static Arr<data::Sampler> readSamplers(const Json::Array& src)
//{
//  Arr<data::Sampler> ret;
//
//  for(const auto& info: src) {
//    auto& sampler = ret.emplace_back();
//
//    sampler.name = info["name"].AsStringOpt();
//
//    switch(info["magFilter"].AsNumberOpt<u32>(0u)) {
//      case 9728u:
//        sampler.magFilter = SamplerFilter::Nearest;
//        break;
//      case 9729u:
//        sampler.magFilter = SamplerFilter::Linear;
//        break;
//      default:
//        sampler.magFilter = SamplerFilter::Nearest;
//        break;
//    }
//
//    switch(info["minFilter"].AsNumberOpt<u32>(0u)) {
//      case 9728u:
//        sampler.magFilter = SamplerFilter::Nearest;
//        sampler.mipFilter = SamplerFilter::None;
//        break;
//      case 9729u:
//        sampler.magFilter = SamplerFilter::Linear;
//        sampler.mipFilter = SamplerFilter::None;
//        break;
//      case 9984u:
//        sampler.magFilter = SamplerFilter::Nearest;
//        sampler.mipFilter = SamplerFilter::Nearest;
//        break;
//      case 9985u:
//        sampler.magFilter = SamplerFilter::Linear;
//        sampler.mipFilter = SamplerFilter::Nearest;
//        break;
//      case 9986u:
//        sampler.magFilter = SamplerFilter::Nearest;
//        sampler.mipFilter = SamplerFilter::Linear;
//        break;
//      case 9987u:
//        sampler.magFilter = SamplerFilter::Linear;
//        sampler.mipFilter = SamplerFilter::Linear;
//        break;
//      default:
//        sampler.magFilter = SamplerFilter::Nearest;
//        sampler.mipFilter = SamplerFilter::None;
//        break;
//    }
//
//    switch(info["wrapS"].AsNumberOpt<u32>(0u)) {
//      case 33071u:
//        sampler.wrapU = SamplerWrap::Clamp;
//        break;
//      case 33648u:
//        sampler.wrapU = SamplerWrap::RepeatMirrored;
//        break;
//      case 10497:
//        sampler.wrapU = SamplerWrap::Repeat;
//        break;
//      default:
//        sampler.wrapU = SamplerWrap::Repeat;
//        break;
//    }
//
//    switch(info["wrapT"].AsNumberOpt<u32>(0u)) {
//      case 33071u:
//        sampler.wrapV = SamplerWrap::Clamp;
//        break;
//      case 33648u:
//        sampler.wrapV = SamplerWrap::RepeatMirrored;
//        break;
//      case 10497:
//        sampler.wrapV = SamplerWrap::Repeat;
//        break;
//      default:
//        sampler.wrapV = SamplerWrap::Repeat;
//        break;
//    }
//  }
//
//  return ret;
//}
//
// static Arr<data::Texture> readTextures(const Json::Array& src)
//{
//  Arr<data::Texture> ret;
//
//  for(const auto& info: src) {
//    auto& texture = ret.emplace_back();
//
//    texture.name         = info["name"].AsStringOpt();
//    texture.imageIndex   = info["source"].AsNumberOpt<u32>();
//    texture.samplerIndex = info["sampler"].AsNumberOpt<u32>();
//  }
//
//  return ret;
//}
//
// static Opt<data::TextureRef> readTextureRef(const Json::Value& info)
//{
//  if(!info.IsObject())
//    return std::nullopt;
//
//  data::TextureRef ret;
//  ret.textureIndex = info["index"].AsNumber<u32>();
//
//  if(info["scale"].IsNumber())
//    ret.scale = info["scale"].AsNumberOpt<f32>(1.0f);
//  else if(info["strength"].IsNumber())
//    ret.scale = info["strength"].AsNumberOpt<f32>(1.0f);
//
//  ret.texCoord = info["texCoord"].AsNumberOpt<u32>(0u);
//
//  return ret;
//}
//
// static Arr<data::Material> readMaterials(const Json::Array& src)
//{
//  Arr<data::Material> ret;
//
//  for(const auto& matInfo: src) {
//    auto& material = ret.emplace_back();
//
//    material.name = matInfo["name"].AsStringOpt();
//
//    if(matInfo["pbrMetallicRoughness"].IsObject()) {
//      const auto& pbrInfo = matInfo["pbrMetallicRoughness"];
//
//      data::PbrMetallicRoughness param;
//      param.baseColorFactor          =
//      pbrInfo["baseColorFactor"].AsVectorOpt<Float4>({1, 1, 1, 1});
//      param.baseColorTexture         =
//      readTextureRef(pbrInfo["baseColorTexture"]);
//      param.metallicFactor           =
//      pbrInfo["metallicFactor"].AsNumberOpt<f32>(1.0f);
//      param.roughnessFactor          =
//      pbrInfo["roughnessFactor"].AsNumberOpt<f32>(1.0f);
//      param.metallicRoughnessTexture =
//      readTextureRef(pbrInfo["metallicRoughnessTexture"]);
//
//      material.model = param;
//    }
//
//    if(matInfo["extensions"]["KHR_materials_pbrSpecularGlossiness"].IsObject())
//    {
//      const auto& pbrInfo =
//      matInfo["extensions"]["KHR_materials_pbrSpecularGlossiness"];
//
//      data::PbrSpecularGlossiness param;
//      param.diffuseFactor             =
//      pbrInfo["diffuseFactor"].AsVectorOpt<Float4>({1, 1, 1, 1});
//      param.diffuseTexture            =
//      readTextureRef(pbrInfo["diffuseTexture"]); param.specularFactor
//      = pbrInfo["specularFactor"].AsVectorOpt<Float3>({1, 1, 1});
//      param.glossinessFactor          =
//      pbrInfo["glossinessFactor"].AsNumberOpt<f32>(1.0f);
//      param.specularGlossinessTexture =
//      readTextureRef(pbrInfo["specularGlossinessTexture"]);
//
//      material.model = param;
//    }
//
//    material.normalTexture    =
//    readTextureRef(matInfo["normalTexture"]);
//    material.occlusionTexture =
//    readTextureRef(matInfo["occlusionTexture"]);
//    material.emissiveTexture  =
//    readTextureRef(matInfo["emissiveTexture"]);
//    material.emissiveFactor   =
//    matInfo["emissiveFactor"].AsVector<Float3>();
//
//    material.alphaMode = AlphaMode::Opaque;
//    if(matInfo["alphaMode"].IsString()) {
//      if(matInfo["alphaMode"].AsString() == "MASK")
//        material.alphaMode = AlphaMode::Mask;
//      if(matInfo["alphaMode"].AsString() == "BLEND")
//        material.alphaMode = AlphaMode::Blend;
//    }
//
//    material.doubleSided = matInfo["doubleSided"].AsBoolOpt(false);
//  }
//
//  return ret;
//}
//
// data::Model DataReader::readGLTF(std::istream& src, const
// ModelOptions& options)
//{
//  data::Model out;
//
//  Json::Value json;
//  json.Read(src);
//
//  if(json["buffers"].IsArray())
//    out.buffers = readBuffers(json["buffers"].AsArray(), m_uriFilter,
//    m_fileReader, options);
//
//  if(json["bufferViews"].IsArray())
//    out.bufferViews = readBufferViews(json["bufferViews"].AsArray());
//
//  if(json["meshes"].IsArray())
//    out.meshes = readMeshes(json["meshes"].AsArray());
//
//  if(json["accessors"].IsArray())
//    out.accessors = readAccessors(json["accessors"].AsArray());
//
//  if(json["images"].IsArray())
//    out.images = readImages(json["images"].AsArray(), *this,
//    m_uriFilter, options);
//
//  if(json["samplers"].IsArray())
//    out.samplers = readSamplers(json["samplers"].AsArray());
//
//  if(json["textures"].IsArray())
//    out.textures = readTextures(json["textures"].AsArray());
//
//  if(json["materials"].IsArray())
//    out.materials = readMaterials(json["materials"].AsArray());
//
//  return out;
//}
