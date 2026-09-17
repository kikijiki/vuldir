#include "Scene.hpp"

#include "IblMath.hpp"
#include "Ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>

using namespace vd;

Arr<char> ReadFile(char const* path)
{
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if(!ifs) return {};

  const auto pos = ifs.tellg();
  Arr<char>  buf(pos);

  ifs.seekg(0, std::ios::beg);
  ifs.read(buf.data(), pos);

  return buf;
}

UPtr<Shader> LoadShader(Device& device, const char* path)
{
  const auto data = ReadFile(path);
  if(data.empty()) throw std::runtime_error("Could not find the file");

  return std::make_unique<Shader>(device, data);
}

// CPU IBL helpers (equirectangular, split-sum)
namespace {

constexpr f32 kPi = 3.14159265358979323846f;

struct HdrImage {
  u32      width  = 0;
  u32      height = 0;
  Arr<f32> rgba; // width*height*4
};

Float3 sampleEquirect(const HdrImage& img, Float3 dir)
{
  const auto uv = vd::sample::DirectionToEquirectUV(dir);
  f32        u  = uv[0];
  f32        v  = uv[1];
  u             = u - std::floor(u);
  v             = std::clamp(v, 0.f, 1.f);

  const f32 x  = u * static_cast<f32>(img.width - 1);
  const f32 y  = v * static_cast<f32>(img.height - 1);
  const u32 x0 = static_cast<u32>(x);
  const u32 y0 = static_cast<u32>(y);
  const u32 x1 = std::min(x0 + 1u, img.width - 1u);
  const u32 y1 = std::min(y0 + 1u, img.height - 1u);
  const f32 fx = x - static_cast<f32>(x0);
  const f32 fy = y - static_cast<f32>(y0);

  auto at = [&](u32 ix, u32 iy) {
    const size_t i = (static_cast<size_t>(iy) * img.width + ix) * 4u;
    return Float3{img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
  };

  const Float3 c00 = at(x0, y0);
  const Float3 c10 = at(x1, y0);
  const Float3 c01 = at(x0, y1);
  const Float3 c11 = at(x1, y1);
  const Float3 c0  = mt::Add(mt::Mul(c00, 1.f - fx), mt::Mul(c10, fx));
  const Float3 c1  = mt::Add(mt::Mul(c01, 1.f - fx), mt::Mul(c11, fx));
  return mt::Add(mt::Mul(c0, 1.f - fy), mt::Mul(c1, fy));
}

Float3 equirectDir(f32 u, f32 v)
{
  return vd::sample::EquirectUVToDirection({u, v});
}

HdrImage makeProceduralSky(u32 w, u32 h)
{
  HdrImage img;
  img.width  = w;
  img.height = h;
  img.rgba.resize(static_cast<size_t>(w) * h * 4u);
  for(u32 y = 0; y < h; ++y) {
    for(u32 x = 0; x < w; ++x) {
      const f32 u   = (x + 0.5f) / w;
      const f32 v   = (y + 0.5f) / h;
      Float3    dir = equirectDir(u, v);
      // Gradient sky + soft ground
      const f32 hemi = std::clamp(dir[1] * 0.5f + 0.5f, 0.f, 1.f);
      Float3    zenith{0.15f, 0.35f, 0.75f};
      Float3    horizon{0.85f, 0.90f, 1.00f};
      Float3    ground{0.08f, 0.07f, 0.06f};
      Float3    col =
        dir[1] >= 0.f
             ? (mt::Add(
              mt::Mul(horizon, 1.f - hemi), mt::Mul(zenith, hemi)))
             : (mt::Add(
              mt::Mul(horizon, 1.f + dir[1]),
              mt::Mul(ground, -dir[1])));
      // Sun disc
      Float3 sunDir = mt::Norm(Float3{0.3f, 0.85f, 0.2f});
      f32    sun = std::pow(std::max(mt::Dot(dir, sunDir), 0.f), 256.f);
      col        = mt::Add(col, mt::Mul(Float3{20.f, 18.f, 12.f}, sun));

      const size_t i  = (static_cast<size_t>(y) * w + x) * 4u;
      img.rgba[i + 0] = col[0];
      img.rgba[i + 1] = col[1];
      img.rgba[i + 2] = col[2];
      img.rgba[i + 3] = 1.f;
    }
  }
  return img;
}

HdrImage resizeEquirect(const HdrImage& src, u32 w, u32 h)
{
  HdrImage dst;
  dst.width  = w;
  dst.height = h;
  dst.rgba.resize(static_cast<size_t>(w) * h * 4u);
  for(u32 y = 0; y < h; ++y) {
    for(u32 x = 0; x < w; ++x) {
      const f32    u   = (x + 0.5f) / w;
      const f32    v   = (y + 0.5f) / h;
      const Float3 col = sampleEquirect(src, equirectDir(u, v));
      const size_t i   = (static_cast<size_t>(y) * w + x) * 4u;
      dst.rgba[i + 0]  = col[0];
      dst.rgba[i + 1]  = col[1];
      dst.rgba[i + 2]  = col[2];
      dst.rgba[i + 3]  = 1.f;
    }
  }
  return dst;
}

Float2 hammersley(u32 i, u32 n)
{
  u32 bits = i;
  bits     = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  const f32 rdi = static_cast<f32>(bits) * 2.3283064365386963e-10f;
  return {static_cast<f32>(i) / static_cast<f32>(n), rdi};
}

Float3 importanceSampleGGX(Float2 xi, f32 roughness, Float3 N)
{
  const f32 a   = roughness * roughness;
  const f32 phi = 2.f * kPi * xi[0];
  const f32 cosTheta =
    std::sqrt((1.f - xi[1]) / (1.f + (a * a - 1.f) * xi[1]));
  const f32 sinTheta =
    std::sqrt(std::max(0.f, 1.f - cosTheta * cosTheta));

  Float3 H{
    std::cos(phi) * sinTheta, cosTheta, std::sin(phi) * sinTheta};

  Float3 up =
    std::abs(N[1]) < 0.999f ? Float3{0, 1, 0} : Float3{1, 0, 0};
  Float3 tangent   = mt::Norm(mt::Cross(up, N));
  Float3 bitangent = mt::Cross(N, tangent);
  return mt::Norm(
    mt::Add(
      mt::Add(mt::Mul(tangent, H[0]), mt::Mul(N, H[1])),
      mt::Mul(bitangent, H[2])));
}

HdrImage
convolveIrradiance(const HdrImage& env, u32 w, u32 h, u32 samples)
{
  HdrImage out;
  out.width  = w;
  out.height = h;
  out.rgba.resize(static_cast<size_t>(w) * h * 4u);

  for(u32 y = 0; y < h; ++y) {
    for(u32 x = 0; x < w; ++x) {
      const Float3 N = equirectDir((x + 0.5f) / w, (y + 0.5f) / h);
      Float3       up =
        std::abs(N[1]) < 0.999f ? Float3{0, 1, 0} : Float3{1, 0, 0};
      Float3 tangent   = mt::Norm(mt::Cross(up, N));
      Float3 bitangent = mt::Cross(N, tangent);

      Float3 irradiance{0.f, 0.f, 0.f};
      f32    weight = 0.f;
      for(u32 i = 0; i < samples; ++i) {
        Float2 xi = hammersley(i, samples);
        // Cosine-weighted hemisphere
        const f32 phi      = 2.f * kPi * xi[0];
        const f32 cosTheta = std::sqrt(1.f - xi[1]);
        const f32 sinTheta = std::sqrt(xi[1]);
        Float3    Llocal{
          std::cos(phi) * sinTheta, cosTheta, std::sin(phi) * sinTheta};
        Float3 L = mt::Norm(
          mt::Add(
            mt::Add(mt::Mul(tangent, Llocal[0]), mt::Mul(N, Llocal[1])),
            mt::Mul(bitangent, Llocal[2])));
        const f32 NdotL = std::max(mt::Dot(N, L), 0.f);
        if(NdotL > 0.f) {
          irradiance =
            mt::Add(irradiance, mt::Mul(sampleEquirect(env, L), NdotL));
          weight += NdotL;
        }
      }
      if(weight > 0.f) irradiance = mt::Mul(irradiance, 1.f / weight);

      const size_t idx  = (static_cast<size_t>(y) * w + x) * 4u;
      out.rgba[idx + 0] = irradiance[0];
      out.rgba[idx + 1] = irradiance[1];
      out.rgba[idx + 2] = irradiance[2];
      out.rgba[idx + 3] = 1.f;
    }
  }
  return out;
}

HdrImage prefilterMip(
  const HdrImage& env, u32 w, u32 h, f32 roughness, u32 samples)
{
  HdrImage out;
  out.width  = w;
  out.height = h;
  out.rgba.resize(static_cast<size_t>(w) * h * 4u);

  roughness = std::max(roughness, 0.04f);

  for(u32 y = 0; y < h; ++y) {
    for(u32 x = 0; x < w; ++x) {
      const Float3 N = equirectDir((x + 0.5f) / w, (y + 0.5f) / h);
      const Float3 R = N;
      const Float3 V = R;

      Float3 prefiltered{0.f, 0.f, 0.f};
      f32    totalWeight = 0.f;
      for(u32 i = 0; i < samples; ++i) {
        Float2 xi = hammersley(i, samples);
        Float3 H  = importanceSampleGGX(xi, roughness, N);
        Float3 L  = mt::Norm(
          mt::Add(mt::Mul(H, 2.f * mt::Dot(V, H)), mt::Mul(V, -1.f)));
        f32 NdotL = std::max(mt::Dot(N, L), 0.f);
        if(NdotL > 0.f) {
          prefiltered = mt::Add(
            prefiltered, mt::Mul(sampleEquirect(env, L), NdotL));
          totalWeight += NdotL;
        }
      }
      if(totalWeight > 0.f)
        prefiltered = mt::Mul(prefiltered, 1.f / totalWeight);

      const size_t idx  = (static_cast<size_t>(y) * w + x) * 4u;
      out.rgba[idx + 0] = prefiltered[0];
      out.rgba[idx + 1] = prefiltered[1];
      out.rgba[idx + 2] = prefiltered[2];
      out.rgba[idx + 3] = 1.f;
    }
  }
  return out;
}

f32 geometrySchlickGGX(f32 NdotV, f32 roughness)
{
  // IBL uses k = a^2 / 2
  const f32 a = roughness;
  const f32 k = (a * a) / 2.f;
  return NdotV / (NdotV * (1.f - k) + k);
}

f32 geometrySmith(f32 NdotV, f32 NdotL, f32 roughness)
{
  return geometrySchlickGGX(NdotV, roughness) *
         geometrySchlickGGX(NdotL, roughness);
}

HdrImage generateBrdfLut(u32 size, u32 samples)
{
  HdrImage out;
  out.width  = size;
  out.height = size;
  out.rgba.resize(static_cast<size_t>(size) * size * 4u);

  for(u32 y = 0; y < size; ++y) {
    for(u32 x = 0; x < size; ++x) {
      const f32 NdotV     = std::max((x + 0.5f) / size, 1e-4f);
      const f32 roughness = std::max((y + 0.5f) / size, 1e-4f);

      Float3 V{std::sqrt(1.f - NdotV * NdotV), NdotV, 0.f};
      Float3 N{0.f, 1.f, 0.f};

      f32 A = 0.f;
      f32 B = 0.f;
      for(u32 i = 0; i < samples; ++i) {
        Float2 xi = hammersley(i, samples);
        Float3 H  = importanceSampleGGX(xi, roughness, N);
        Float3 L  = mt::Norm(
          mt::Add(mt::Mul(H, 2.f * mt::Dot(V, H)), mt::Mul(V, -1.f)));

        const f32 NdotL = std::max(L[1], 0.f);
        const f32 NdotH = std::max(H[1], 0.f);
        const f32 VdotH = std::max(mt::Dot(V, H), 0.f);

        if(NdotL > 0.f) {
          const f32 G = geometrySmith(NdotV, NdotL, roughness);
          const f32 G_Vis =
            (G * VdotH) / std::max(NdotH * NdotV, 1e-4f);
          const f32 Fc = std::pow(1.f - VdotH, 5.f);
          A += (1.f - Fc) * G_Vis;
          B += Fc * G_Vis;
        }
      }
      A /= static_cast<f32>(samples);
      B /= static_cast<f32>(samples);

      const size_t idx  = (static_cast<size_t>(y) * size + x) * 4u;
      out.rgba[idx + 0] = A;
      out.rgba[idx + 1] = B;
      out.rgba[idx + 2] = 0.f;
      out.rgba[idx + 3] = 1.f;
    }
  }
  return out;
}

Arr<u8> hdrToBytes(const HdrImage& img)
{
  Arr<u8> bytes(img.rgba.size() * sizeof(f32));
  std::memcpy(bytes.data(), img.rgba.data(), bytes.size());
  return bytes;
}

Arr<u8> buildMipChainBytes(const Arr<HdrImage>& mips)
{
  size_t total = 0;
  for(const auto& m: mips) total += m.rgba.size() * sizeof(f32);
  Arr<u8> bytes(total);
  size_t  offset = 0;
  for(const auto& m: mips) {
    const size_t n = m.rgba.size() * sizeof(f32);
    std::memcpy(bytes.data() + offset, m.rgba.data(), n);
    offset += n;
  }
  return bytes;
}

Format toSrgbFormat(Format fmt)
{
  if(fmt == Format::R8G8B8A8_UNORM) return Format::R8G8B8A8_SRGB;
  if(fmt == Format::B8G8R8A8_UNORM) return Format::B8G8R8A8_SRGB;
  return fmt;
}

// glTF quaternion (xyzw) to rotation matrix (column vectors, row-major storage).
Float44 quatToMatrix(Float4 q)
{
  const f32 x = q[0], y = q[1], z = q[2], w = q[3];
  const f32 x2 = x + x, y2 = y + y, z2 = z + z;
  const f32 xx = x * x2, yy = y * y2, zz = z * z2;
  const f32 xy = x * y2, xz = x * z2, yz = y * z2;
  const f32 wx = w * x2, wy = w * y2, wz = w * z2;

  Float44 m = mt::Identity<Float44>();
  m[0][0]   = 1.f - (yy + zz);
  m[0][1]   = xy - wz;
  m[0][2]   = xz + wy;
  m[1][0]   = xy + wz;
  m[1][1]   = 1.f - (xx + zz);
  m[1][2]   = yz - wx;
  m[2][0]   = xz - wy;
  m[2][1]   = yz + wx;
  m[2][2]   = 1.f - (xx + yy);
  return m;
}

Float44 nodeLocalMatrix(const data::Node& node)
{
  if(node.matrix) return *node.matrix;

  Float44 t = mt::Identity<Float44>();
  Float44 r = mt::Identity<Float44>();
  Float44 s = mt::Identity<Float44>();
  if(node.translation) t = mt::Translation(*node.translation);
  if(node.rotation) r = quatToMatrix(*node.rotation);
  if(node.scale) s = mt::Scaling(*node.scale);
  // glTF local = T * R * S
  return mt::Mul(t, mt::Mul(r, s));
}

Arr<Float44> computeMeshWorldMatrices(const data::Model& model)
{
  Arr<Float44> meshWorld(model.meshes.size(), mt::Identity<Float44>());
  if(model.nodes.empty()) return meshWorld;

  const auto visit = [&](auto&& self, u32 idx, const Float44& parent) {
    if(idx >= model.nodes.size()) return;
    const auto& node  = model.nodes[idx];
    const auto  world = mt::Mul(parent, nodeLocalMatrix(node));
    if(node.mesh && *node.mesh < meshWorld.size()) {
      meshWorld[*node.mesh] = world;
    }
    for(u32 child: node.children) self(self, child, world);
  };

  const auto identity = mt::Identity<Float44>();
  if(!model.rootNodes.empty()) {
    for(u32 root: model.rootNodes) visit(visit, root, identity);
  } else {
    for(u32 i = 0; i < model.nodes.size(); ++i)
      visit(visit, i, identity);
  }
  return meshWorld;
}

} // namespace

namespace vd {

Shader* Scene::addShader(Device& dev, const char* path)
{
  shaders.push_back(LoadShader(dev, path));
  return shaders.back().get();
}

Scene::Scene(Device& dev, u32 frameCount)
{
  frames.resize(frameCount);

  auto depth = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "Depth",
           .VS   = addShader(dev, "Shaders/RP_Depth.vs.cso"),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats       = {},
           .depthTestEnable    = true,
           .depthWriteEnable   = true,
           .depthCompareOp     = CompareOp::Less,
           .dynamicViewport    = true,
           .dynamicScissor     = true});

  for(auto& frame: frames) {
    frame.passes[PassDepth].id       = PassDepth;
    frame.passes[PassDepth].pipeline = depth.get();
  }
  pipelines.push_back(std::move(depth));

  Pipeline::AttachmentBlendDesc gbufferBlend{
    .blendEnable = false,
    .writeR      = true,
    .writeG      = true,
    .writeB      = true,
    .writeA      = true};

  auto gbuffer = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "GBuffer",
           .VS   = addShader(dev, "Shaders/RP_GBuffer.vs.cso"),
           .PS   = addShader(dev, "Shaders/RP_GBuffer.ps.cso"),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats =
             {std::begin(GBufferFormats), std::end(GBufferFormats)},
           .depthTestEnable = true,
           .depthCompareOp  = CompareOp::LessOrEqual,
           .blendAttachments =
             {gbufferBlend, gbufferBlend, gbufferBlend, gbufferBlend},
           .dynamicViewport = true,
           .dynamicScissor  = true});

  for(auto& frame: frames) {
    frame.passes[PassGBuffer].id       = PassGBuffer;
    frame.passes[PassGBuffer].pipeline = gbuffer.get();
  }
  pipelines.push_back(std::move(gbuffer));

  auto lighting = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "Lighting",
           .VS   = addShader(dev, "Shaders/RP_Lighting.vs.cso"),
           .PS   = addShader(dev, "Shaders/RP_Lighting.ps.cso"),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats       = {Format::B8G8R8A8_UNORM},
           .blendAttachments   = {{}},
           .dynamicViewport    = true,
           .dynamicScissor     = true});

  for(auto& frame: frames) {
    frame.passes[PassLighting].id       = PassLighting;
    frame.passes[PassLighting].pipeline = lighting.get();
  }
  pipelines.push_back(std::move(lighting));

  // RmlUi outputs premultiplied vertex and texture colors.
  Pipeline::AttachmentBlendDesc uiBlend{
    .blendEnable         = true,
    .srcColorBlendFactor = BlendFactor::ONE,
    .dstColorBlendFactor = BlendFactor::ONE_MINUS_SRC_ALPHA,
    .srcAlphaBlendFactor = BlendFactor::ONE,
    .dstAlphaBlendFactor = BlendFactor::ONE_MINUS_SRC_ALPHA,
    .writeR              = true,
    .writeG              = true,
    .writeB              = true,
    .writeA              = true};

  auto uiOverlay = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "UiOverlay",
           .VS   = addShader(dev, "Shaders/RP_UiOverlay.vs.cso"),
           .PS   = addShader(dev, "Shaders/RP_UiOverlay.ps.cso"),
           .colorFormats     = {Format::B8G8R8A8_UNORM},
           .blendAttachments = {uiBlend},
           .dynamicViewport  = true,
           .dynamicScissor   = true});

  for(auto& frame: frames) {
    frame.passes[PassUi].id       = PassUi;
    frame.passes[PassUi].pipeline = uiOverlay.get();
  }
  pipelines.push_back(std::move(uiOverlay));

  Pipeline::AttachmentBlendDesc aaBlend{
    .blendEnable = false,
    .writeR      = true,
    .writeG      = true,
    .writeB      = true,
    .writeA      = true};

  auto makeAaPipeline = [&](const char* name, const char* shaderStem,
                            Format colorFormat) {
    const Str vsPath =
      formatString("Shaders/%s.vs.cso", shaderStem);
    const Str psPath =
      formatString("Shaders/%s.ps.cso", shaderStem);
    return std::make_unique<Pipeline>(
      dev, Pipeline::GraphicsDesc{
             .name             = name,
             .VS               = LoadShader(dev, vsPath.c_str()).get(),
             .PS               = LoadShader(dev, psPath.c_str()).get(),
             .colorFormats     = {colorFormat},
             .blendAttachments = {aaBlend},
             .dynamicViewport  = true,
             .dynamicScissor   = true});
  };

  aaPipelines.resize(static_cast<size_t>(AaPipeline::Count));
  aaPipelines[static_cast<size_t>(AaPipeline::Resolve)] =
    makeAaPipeline("AaResolve", "RP_Resolve", Format::B8G8R8A8_UNORM);
  aaPipelines[static_cast<size_t>(AaPipeline::Fxaa)] =
    makeAaPipeline("AaFxaa", "RP_Fxaa", Format::B8G8R8A8_UNORM);
  aaPipelines[static_cast<size_t>(AaPipeline::SmaaEdge)] =
    makeAaPipeline("AaSmaaEdge", "RP_SmaaEdge", Format::R8G8B8A8_UNORM);
  aaPipelines[static_cast<size_t>(AaPipeline::SmaaWeights)] =
    makeAaPipeline(
      "AaSmaaWeights", "RP_SmaaWeights", Format::R8G8B8A8_UNORM);
  aaPipelines[static_cast<size_t>(AaPipeline::SmaaBlend)] =
    makeAaPipeline("AaSmaaBlend", "RP_SmaaBlend", Format::B8G8R8A8_UNORM);

  u32 frameIdx = 0u;
  for(auto& frame: frames) {
    frame.scene = std::make_unique<UniformBuffer<SceneParam>>(
      dev, Buffer::Desc{
             .name        = formatString("Scene [frame %u]", frameIdx),
             .usage       = ResourceUsage::ShaderResource,
             .size        = sizeof(SceneParam),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});
    ++frameIdx;
  }
}

void Scene::createSwapchainDependentResources(Device& dev)
{
  const auto scExtent = dev.GetSwapchain().GetExtent();

  u32 frameIdx = 0;
  for(auto& frame: frames) {
    for(auto& pass: frame.passes) { pass.color.clear(); }

    frame.depthStencil.reset();
    frame.depthStencil = std::make_unique<Image>(
      dev, Image::Desc{
             .name  = formatString("DepthStencil [frame %u]", frameIdx),
             .usage = ResourceUsage::DepthStencil |
                      ResourceUsage::ShaderResource,
             .format      = Format::D32_SFLOAT_S8_UINT,
             .dimension   = Dimension::e2D,
             .extent      = {scExtent, 1u},
             .defaultView = ViewType::DSV});

    // Depth attachments
    frame.passes[PassDepth].depthStencil = {
      .view       = frame.depthStencil->GetView(),
      .state      = ResourceState::DepthStencilRW,
      .loadOp     = LoadOp::Clear,
      .storeOp    = StoreOp::Store,
      .clearValue = DepthStencil{1.0f, 0},
    };
    frame.passes[PassGBuffer].depthStencil = {
      .view    = frame.depthStencil->GetView(),
      .state   = ResourceState::DepthStencilRO,
      .loadOp  = LoadOp::Load,
      .storeOp = StoreOp::Store,
    };
    frame.passes[PassLighting].depthStencil = {
      .view    = frame.depthStencil->GetView(),
      .state   = ResourceState::DepthStencilRO,
      .loadOp  = LoadOp::Load,
      .storeOp = StoreOp::Store,
    };

    // GBuffer
    frame.gbuffers.clear();
    for(u32 idx = 0u; idx < GBufferCount; ++idx) {
      auto gbuf = std::make_unique<Image>(
        dev,
        Image::Desc{
          .name = formatString("GBuffer #%u [frame %u]", idx, frameIdx),
          .usage =
            ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
          .format    = GBufferFormats[idx],
          .dimension = Dimension::e2D,
          .extent    = {scExtent, 1u}});

      gbuf->AddView(ViewType::SRV);
      gbuf->AddView(ViewType::RTV);

      frame.passes[PassGBuffer].color.push_back({
        .view    = gbuf->GetView(ViewType::RTV),
        .state   = ResourceState::RenderTarget,
        .loadOp  = LoadOp::Clear,
        .storeOp = StoreOp::Store,
      });

      frame.gbuffers.push_back(std::move(gbuf));
    }

    frame.passes[PassLighting].color.push_back({
      .view    = nullptr, // filled after sceneColor is created below
      .state   = ResourceState::RenderTarget,
      .loadOp  = LoadOp::Clear,
      .storeOp = StoreOp::Store,
    });

    frame.sceneColor = std::make_unique<Image>(
      dev, Image::Desc{
             .name = formatString("SceneColor LDR [frame %u]", frameIdx),
             .usage =
               ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
             .format      = Format::B8G8R8A8_UNORM,
             .dimension   = Dimension::e2D,
             .extent      = {scExtent, 1u}});
    frame.sceneColor->AddView(ViewType::SRV);
    frame.sceneColor->AddView(ViewType::RTV);
    frame.passes[PassLighting].color[0].view =
      frame.sceneColor->GetView(ViewType::RTV);

    frame.smaaEdges = std::make_unique<Image>(
      dev, Image::Desc{
             .name = formatString("SMAA Edges [frame %u]", frameIdx),
             .usage =
               ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
             .format      = Format::R8G8B8A8_UNORM,
             .dimension   = Dimension::e2D,
             .extent      = {scExtent, 1u}});
    frame.smaaEdges->AddView(ViewType::SRV);
    frame.smaaEdges->AddView(ViewType::RTV);

    frame.smaaBlend = std::make_unique<Image>(
      dev, Image::Desc{
             .name = formatString("SMAA Blend [frame %u]", frameIdx),
             .usage =
               ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
             .format      = Format::R8G8B8A8_UNORM,
             .dimension   = Dimension::e2D,
             .extent      = {scExtent, 1u}});
    frame.smaaBlend->AddView(ViewType::SRV);
    frame.smaaBlend->AddView(ViewType::RTV);

    // View is set per frame in render() from the acquired swapchain image.
    // Swapchain images are not indexed by frame slot (there may be fewer).
    frame.passes[PassUi].color.push_back({
      .view    = nullptr,
      .state   = ResourceState::RenderTarget,
      .loadOp  = LoadOp::Load,
      .storeOp = StoreOp::Store,
    });
    // Images were recreated; refresh bindless indices (prepare() runs once).
    if(frame.scene) {
      frame.scene->data.gbufIdx0 = static_cast<i32>(
        frame.gbuffers[0]->GetView(ViewType::SRV)->binding.index);
      frame.scene->data.gbufIdx1 = static_cast<i32>(
        frame.gbuffers[1]->GetView(ViewType::SRV)->binding.index);
      frame.scene->data.gbufIdx2 = static_cast<i32>(
        frame.gbuffers[2]->GetView(ViewType::SRV)->binding.index);
      frame.scene->data.gbufIdx3 = static_cast<i32>(
        frame.gbuffers[3]->GetView(ViewType::SRV)->binding.index);
      frame.scene->data.sceneColorIdx = static_cast<i32>(
        frame.sceneColor->GetView(ViewType::SRV)->binding.index);
      frame.scene->data.aaEdgesIdx = static_cast<i32>(
        frame.smaaEdges->GetView(ViewType::SRV)->binding.index);
      frame.scene->data.aaBlendIdx = static_cast<i32>(
        frame.smaaBlend->GetView(ViewType::SRV)->binding.index);
      if(smaaArea) {
        frame.scene->data.aaAreaIdx = static_cast<i32>(
          smaaArea->GetView(ViewType::SRV)->binding.index);
      }
      if(smaaSearch) {
        frame.scene->data.aaSearchIdx = static_cast<i32>(
          smaaSearch->GetView(ViewType::SRV)->binding.index);
      }
      frame.scene->data.aaInvSize = {
        1.f / static_cast<f32>(scExtent[0]),
        1.f / static_cast<f32>(scExtent[1])};
      frame.scene->data.aaSize = {
        static_cast<f32>(scExtent[0]), static_cast<f32>(scExtent[1])};
      frame.scene->Update();
    }

    ++frameIdx;
  }
}

void Scene::prepare(RenderContext&)
{
  // Camera setup from controls
  const auto& c         = m_controls;
  const f32   cp        = std::cos(c.orbitPitch);
  const f32   sp        = std::sin(c.orbitPitch);
  const f32   cy        = std::cos(c.orbitYaw);
  const f32   sy        = std::sin(c.orbitYaw);
  const auto  cameraPos = Float3{
    c.orbitDistance * cp * cy, c.orbitDistance * sp,
    c.orbitDistance * cp * sy};
  const auto cameraTgt  = Float3{0, 0, 0};
  const auto cameraUp   = Float3{0, 1, 0};
  const auto cameraView = mt::LookAt(cameraPos, cameraTgt, cameraUp);
  const auto cameraProj = mt::PerspectiveFov(c.fovY, 1.f, 0.1f, 1000.f);

  for(auto& frame: frames) {
    frame.scene->data = {
      .view           = cameraView,
      .viewProjection = mt::Mul(cameraView, cameraProj),
      .cameraPosition = cameraPos,
      .cameraAspect   = 1.f,
      .gbufIdx0       = static_cast<i32>(
        frame.gbuffers[0]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx1 = static_cast<i32>(
        frame.gbuffers[1]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx2 = static_cast<i32>(
        frame.gbuffers[2]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx3 = static_cast<i32>(
        frame.gbuffers[3]->GetView(ViewType::SRV)->binding.index),
      .ambientColor              = c.ambientColor,
      .ambientIntensity          = c.ambientIntensity,
      .directionalLightDirection = mt::Norm(c.lightDirection),
      .irradianceMapIdx          = -1,
      .directionalLightColor     = c.lightColor,
      .prefilteredMapIdx         = -1,
      .directionalLightIntensity = c.lightIntensity,
      .brdfLutIdx                = -1,
      .prefilteredMipCount       = 1.f,
      .iblIntensity              = c.iblIntensity,
      .iblEnabled                = c.iblEnabled ? 1 : 0,
      .iblRotationYaw            = c.iblRotationYaw,
      .exposure                  = c.exposure,
      .gamma                     = c.gamma,
      .debugView                 = static_cast<i32>(c.debugView),
      .cameraTanHalfFovY         = std::tan(c.fovY * 0.5f),
      .environmentMapIdx         = -1,
      .sceneColorIdx             = static_cast<i32>(
        frame.sceneColor->GetView(ViewType::SRV)->binding.index),
      .aaInvSize                 = {1.f, 1.f},
      .aaSize                    = {1.f, 1.f},
      .aaEdgesIdx                = static_cast<i32>(
        frame.smaaEdges->GetView(ViewType::SRV)->binding.index),
      .aaBlendIdx                = static_cast<i32>(
        frame.smaaBlend->GetView(ViewType::SRV)->binding.index),
      .aaAreaIdx                 = -1,
      .aaSearchIdx               = -1,
    };
    frame.scene->Update();
  }
}

void Scene::clearModels() { models.clear(); }

void Scene::loadSmaaTextures(RenderContext& ctx)
{
  auto& dev = ctx.GetDevice();

  auto readAsset = [](const char* primary, const char* fallback) {
    auto data = ReadFile(primary);
    if(data.empty()) data = ReadFile(fallback);
    return data;
  };

  const auto areaData = readAsset(
    "smaa/AreaTexRGBA.bin",
    "../../../../sample/assets/smaa/AreaTexRGBA.bin");
  const auto searchData = readAsset(
    "smaa/SearchTexR.bin",
    "../../../../sample/assets/smaa/SearchTexR.bin");
  if(areaData.size() != 160u * 560u * 4u) {
    throw std::runtime_error(
      "SMAA AreaTexRGBA.bin has unexpected size");
  }
  if(searchData.size() != 64u * 16u) {
    throw std::runtime_error(
      "SMAA SearchTexR.bin has unexpected size");
  }

  smaaArea = std::make_unique<Image>(
    dev, Image::Desc{
           .name        = "SMAA AreaTex",
           .usage       = ResourceUsage::ShaderResource,
           .format      = Format::R8G8B8A8_UNORM,
           .dimension   = Dimension::e2D,
           .extent      = {160u, 560u, 1u},
           .defaultView = ViewType::SRV});
  smaaSearch = std::make_unique<Image>(
    dev, Image::Desc{
           .name        = "SMAA SearchTex",
           .usage       = ResourceUsage::ShaderResource,
           .format      = Format::R8_UNORM,
           .dimension   = Dimension::e2D,
           .extent      = {64u, 16u, 1u},
           .defaultView = ViewType::SRV});

  Arr<u8> areaBytes(areaData.begin(), areaData.end());
  Arr<u8> searchBytes(searchData.begin(), searchData.end());
  ctx.Write(*smaaArea, areaBytes);
  ctx.Write(*smaaSearch, searchBytes);

  const i32 areaIdx = static_cast<i32>(
    smaaArea->GetView(ViewType::SRV)->binding.index);
  const i32 searchIdx = static_cast<i32>(
    smaaSearch->GetView(ViewType::SRV)->binding.index);
  for(auto& frame: frames) {
    if(!frame.scene) continue;
    frame.scene->data.aaAreaIdx   = areaIdx;
    frame.scene->data.aaSearchIdx = searchIdx;
    frame.scene->Update();
  }
}

namespace {

u64 componentSize(data::ComponentType type)
{
  switch(type) {
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
  throw std::runtime_error("Unsupported glTF component type");
}

u64 componentCount(data::AccessorType type)
{
  switch(type) {
    case data::AccessorType::Scalar:
      return 1u;
    case data::AccessorType::Vector2:
      return 2u;
    case data::AccessorType::Vector3:
      return 3u;
    case data::AccessorType::Vector4:
    case data::AccessorType::Matrix22:
      return 4u;
    case data::AccessorType::Matrix33:
      return 9u;
    case data::AccessorType::Matrix44:
      return 16u;
  }
  throw std::runtime_error("Unsupported glTF accessor type");
}

const u8* accessorElement(
  const data::Model& model, const data::Accessor& accessor, u32 index)
{
  const auto& view   = model.bufferViews[accessor.bufferViewIndex];
  const auto& buffer = model.buffers[view.bufferIndex];
  const u64 elementSize =
    componentSize(accessor.componentType) * componentCount(accessor.type);
  const u64 stride = view.stride == 0u ? elementSize : view.stride;
  const u64 offset = view.offset + accessor.offset + index * stride;
  // DataReader validates against the declared byteLength, which a
  // uriFilter-skipped buffer does not actually hold. Check the real size.
  if(offset > buffer.data.size() ||
     elementSize > buffer.data.size() - offset)
    throw std::runtime_error("glTF accessor reads past its buffer data");
  return buffer.data.data() + offset;
}

template<typename T>
T readUnaligned(const u8* data)
{
  T value{};
  std::memcpy(&value, data, sizeof(value));
  return value;
}

f32 readComponent(
  const u8* data, data::ComponentType type, bool normalized)
{
  switch(type) {
    case data::ComponentType::Byte: {
      const auto value = readUnaligned<i8>(data);
      return normalized
               ? std::max(static_cast<f32>(value) / 127.f, -1.f)
               : static_cast<f32>(value);
    }
    case data::ComponentType::UnsignedByte: {
      const auto value = readUnaligned<u8>(data);
      return normalized ? static_cast<f32>(value) / 255.f
                        : static_cast<f32>(value);
    }
    case data::ComponentType::Short: {
      const auto value = readUnaligned<i16>(data);
      return normalized
               ? std::max(static_cast<f32>(value) / 32767.f, -1.f)
               : static_cast<f32>(value);
    }
    case data::ComponentType::UnsignedShort: {
      const auto value = readUnaligned<u16>(data);
      return normalized ? static_cast<f32>(value) / 65535.f
                        : static_cast<f32>(value);
    }
    case data::ComponentType::UnsignedInt:
      return static_cast<f32>(readUnaligned<u32>(data));
    case data::ComponentType::Float:
      return readUnaligned<f32>(data);
  }
  throw std::runtime_error("Unsupported glTF component type");
}

Float4 readVector(
  const data::Model& model, const data::Accessor& accessor, u32 index,
  data::AccessorType expectedType)
{
  if(accessor.type != expectedType)
    throw std::runtime_error("Unexpected glTF vertex accessor type");

  Float4 result{};
  const u8* source = accessorElement(model, accessor, index);
  const u64 size   = componentSize(accessor.componentType);
  const u64 count  = componentCount(accessor.type);
  for(u64 component = 0u; component < count; ++component) {
    result[component] = readComponent(
      source + component * size, accessor.componentType,
      accessor.normalized);
  }
  return result;
}

u32 readIndex(
  const data::Model& model, const data::Accessor& accessor, u32 index)
{
  if(accessor.type != data::AccessorType::Scalar || accessor.normalized)
    throw std::runtime_error("Invalid glTF index accessor");

  const u8* source = accessorElement(model, accessor, index);
  switch(accessor.componentType) {
    case data::ComponentType::UnsignedByte:
      return readUnaligned<u8>(source);
    case data::ComponentType::UnsignedShort:
      return readUnaligned<u16>(source);
    case data::ComponentType::UnsignedInt:
      return readUnaligned<u32>(source);
    default:
      throw std::runtime_error("Invalid glTF index component type");
  }
}

u32 packSignedUnit(f32 value)
{
  return static_cast<u32>(
    std::lround(std::clamp(value, -1.f, 1.f) * 127.5f + 127.5f));
}

} // namespace

void Scene::loadGltfModel(RenderContext& ctx, const Str& file)
{
  auto& dev = ctx.GetDevice();

  // Uploads record on the shared graphics command buffer.
  ctx.Reset();

  auto reader = DataReader(DataReader::Desc{});
  auto data   = reader.ReadModel(file, {});

  // Skipped buffers cannot be drawn; fail early instead of per accessor.
  for(const auto& buffer: data.buffers) {
    if(!buffer.HasData())
      throw std::runtime_error(
        "glTF buffer data was not loaded (uri filtered out?)");
  }

  const auto validateTextureRef = [](const Opt<data::TextureRef>& ref) {
    if(ref && ref->texCoord != 0u)
      throw std::runtime_error(
        "Sample renderer only supports TEXCOORD_0 material textures");
  };
  for(const auto& material: data.materials) {
    if(std::holds_alternative<data::PbrSpecularGlossiness>(material.model))
      throw std::runtime_error(
        "Sample renderer does not support specular-glossiness materials");
    if(material.alphaMode != AlphaMode::Opaque)
      throw std::runtime_error(
        "Sample renderer only supports opaque glTF materials");
    if(
      const auto* pbr =
        std::get_if<data::PbrMetallicRoughness>(&material.model)) {
      validateTextureRef(pbr->baseColorTexture);
      validateTextureRef(pbr->metallicRoughnessTexture);
    }
    validateTextureRef(material.normalTexture);
    validateTextureRef(material.occlusionTexture);
    validateTextureRef(material.emissiveTexture);
  }

  // Auto-fit so the largest AABB axis is kTargetExtent (orbit camera radius is 2).
  constexpr f32 kTargetExtent = 1.5f;
  f32           minX = 1e30f, minY = 1e30f, minZ = 1e30f;
  f32           maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
  bool          haveBounds = false;
  for(const auto& dataMesh: data.meshes) {
    for(const auto& prim: dataMesh.primitives) {
      for(const auto& attr: prim.attributes) {
        if(attr.type != VertexAttribute::Position) continue;
        const auto& acc    = data.accessors[attr.accessorIndex];
        for(u32 i = 0; i < acc.count; ++i) {
          const auto position = readVector(
            data, acc, i, data::AccessorType::Vector3);
          const f32 x = position[0];
          const f32 y = position[1];
          const f32 z = position[2];
          minX        = std::min(minX, x);
          minY        = std::min(minY, y);
          minZ        = std::min(minZ, z);
          maxX        = std::max(maxX, x);
          maxY        = std::max(maxY, y);
          maxZ        = std::max(maxZ, z);
          haveBounds  = true;
        }
      }
    }
  }
  f32 scale = 1.f;
  if(haveBounds) {
    const f32 maxExtent =
      std::max({maxX - minX, maxY - minY, maxZ - minZ});
    if(maxExtent > 1e-6f) scale = kTargetExtent / maxExtent;
  }

  Model model;
  model.name   = fs::path(file).stem().string();
  model.source = file;

  const Arr<Float44> meshWorlds = computeMeshWorldMatrices(data);

  // Which glTF images are color (sRGB) vs data (UNORM).
  Arr<bool> imageIsSrgb(data.images.size(), false);
  auto      markSrgbTex = [&](const Opt<data::TextureRef>& ref) {
    if(!ref) return;
    if(ref->textureIndex >= data.textures.size()) return;
    const auto& tex = data.textures[ref->textureIndex];
    if(!tex.imageIndex || *tex.imageIndex >= imageIsSrgb.size()) return;
    imageIsSrgb[*tex.imageIndex] = true;
  };
  for(const auto& mat: data.materials) {
    if(
      const auto* pbr =
        std::get_if<data::PbrMetallicRoughness>(&mat.model)) {
      markSrgbTex(pbr->baseColorTexture);
    }
    markSrgbTex(mat.emissiveTexture);
  }

  auto resolveImageIndex = [&](u32 textureIndex) -> Opt<u32> {
    if(textureIndex >= data.textures.size()) return std::nullopt;
    const auto& tex = data.textures[textureIndex];
    if(!tex.imageIndex || *tex.imageIndex >= model.textures.size())
      return std::nullopt;
    return tex.imageIndex;
  };

  auto textureSrvIndex = [&](u32 textureIndex) -> i32 {
    const auto img = resolveImageIndex(textureIndex);
    if(!img) return -1;
    return static_cast<i32>(
      model.textures[*img]->GetView(ViewType::SRV)->binding.index);
  };

  for(u32 imgIdx = 0; imgIdx < data.images.size(); ++imgIdx) {
    const auto& image = data.images[imgIdx];
    Format      format =
      imageIsSrgb[imgIdx] ? toSrgbFormat(image.format) : image.format;

    auto tex = std::make_unique<Image>(
      dev, Image::Desc{
             .name        = image.uri ? *image.uri : "gltf texture",
             .usage       = ResourceUsage::ShaderResource,
             .format      = format,
             .dimension   = Dimension::e2D,
             .extent      = {image.size[0], image.size[1], 1u},
             .defaultView = ViewType::SRV,
             .mips        = 1u});

    ctx.Write(*tex, image.texels);
    model.textures.push_back(std::move(tex));
  }

  // Create materials upfront
  for(u32 matIdx = 0; matIdx < data.materials.size(); ++matIdx) {
    auto material = std::make_unique<UniformBuffer<MaterialParam>>(
      dev, Buffer::Desc{
             .name        = "Material buffer",
             .usage       = ResourceUsage::ShaderResource,
             .size        = sizeof(MaterialParam),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});

    material->data = {
      .colIdx          = -1,
      .nrmIdx          = -1,
      .mrIdx           = -1,
      .aoIdx           = -1,
      .emissiveIdx     = -1,
      .metallic        = 1.f,
      .roughness       = 1.f,
      .aoStrength      = 1.f,
      .baseColorFactor = {1.f, 1.f, 1.f, 1.f},
      .emissiveFactor  = {0.f, 0.f, 0.f},
    };

    const auto& mat = data.materials[matIdx];
    model.materialNames.push_back(
      mat.name.value_or(formatString("Material %u", matIdx)));
    if(
      const auto* pbrMat =
        std::get_if<data::PbrMetallicRoughness>(&mat.model)) {
      material->data.metallic        = pbrMat->metallicFactor;
      material->data.roughness       = pbrMat->roughnessFactor;
      material->data.baseColorFactor = pbrMat->baseColorFactor;

      if(pbrMat->baseColorTexture) {
        material->data.colIdx =
          textureSrvIndex(pbrMat->baseColorTexture->textureIndex);
      }
      if(pbrMat->metallicRoughnessTexture) {
        material->data.mrIdx = textureSrvIndex(
          pbrMat->metallicRoughnessTexture->textureIndex);
      }
    }

    if(mat.normalTexture) {
      material->data.nrmIdx =
        textureSrvIndex(mat.normalTexture->textureIndex);
    }
    if(mat.occlusionTexture) {
      material->data.aoIdx =
        textureSrvIndex(mat.occlusionTexture->textureIndex);
      if(mat.occlusionTexture->scale)
        material->data.aoStrength = *mat.occlusionTexture->scale;
    }
    material->data.emissiveFactor = mat.emissiveFactor;
    if(mat.emissiveTexture) {
      material->data.emissiveIdx =
        textureSrvIndex(mat.emissiveTexture->textureIndex);
    }

    material->Update();
    model.materials.push_back(std::move(material));
  }

  u32 defaultMaterialIndex = MaxU32;
  const bool needsDefaultMaterial = std::ranges::any_of(
    data.meshes, [](const data::Mesh& mesh) {
      return std::ranges::any_of(
        mesh.primitives,
        [](const data::MeshPrimitive& primitive) {
          return !primitive.material;
        });
    });
  if(needsDefaultMaterial) {
    defaultMaterialIndex = size32(model.materials);
    auto material = std::make_unique<UniformBuffer<MaterialParam>>(
      dev, Buffer::Desc{
             .name        = "Default material buffer",
             .usage       = ResourceUsage::ShaderResource,
             .size        = sizeof(MaterialParam),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});
    material->data = {
      .colIdx          = -1,
      .nrmIdx          = -1,
      .mrIdx           = -1,
      .aoIdx           = -1,
      .emissiveIdx     = -1,
      .metallic        = 1.f,
      .roughness       = 1.f,
      .aoStrength      = 1.f,
      .baseColorFactor = {1.f, 1.f, 1.f, 1.f},
      .emissiveFactor  = {0.f, 0.f, 0.f},
    };
    material->Update();
    model.materialNames.emplace_back("Default");
    model.materials.push_back(std::move(material));
  }

  for(u32 meshIdx = 0; meshIdx < data.meshes.size(); ++meshIdx) {
    const auto& dataMesh = data.meshes[meshIdx];
    Mesh        mesh;
    mesh.name =
      dataMesh.name.value_or(formatString("Mesh %u", meshIdx));

    const Float44 meshWorld = (meshIdx < meshWorlds.size())
                                ? meshWorlds[meshIdx]
                                : mt::Identity<Float44>();

    for(const auto& prim: dataMesh.primitives) {
      const u32 materialIndex =
        prim.material.value_or(defaultMaterialIndex);
      if(materialIndex >= model.materials.size())
        throw std::runtime_error("glTF primitive material is unavailable");

      Prim primitive;
      primitive.materialIndex = materialIndex;
      primitive.materialName  = model.materialNames[materialIndex];

      primitive.param = std::make_unique<UniformBuffer<PrimParam>>(
        dev, Buffer::Desc{
               .name        = "Primitive parameters",
               .usage       = ResourceUsage::ShaderResource,
               .size        = sizeof(PrimParam),
               .defaultView = ViewType::SRV,
               .memoryType  = MemoryType::Upload});

      primitive.param->data.world =
        mt::Mul(meshWorld, mt::Scaling(scale));
      primitive.param->data.vbPosIdx = -1;
      primitive.param->data.vbNrmIdx = -1;
      primitive.param->data.vbTanIdx = -1;
      primitive.param->data.vbUV0Idx = -1;
      primitive.param->data.materialIdx =
        static_cast<i32>(model.materials[materialIndex]
                           ->buffer->GetView(ViewType::SRV)
                           ->binding.index);

      for(const auto& attr: prim.attributes) {
        if(attr.type == VertexAttribute::Position) {
          Arr<Float4> vertices;

          const auto& acc    = data.accessors[attr.accessorIndex];
          for(u32 i = 0; i < acc.count; i++) {
            const auto position = readVector(
              data, acc, i, data::AccessorType::Vector3);
            const f32 x = position[0];
            const f32 y = position[1];
            const f32 z = position[2];

            vertices.push_back({x, y, z, 1.f});
          }

          if(vertices.empty()) continue;
          primitive.vertexCount = static_cast<u32>(vertices.size());

          primitive.vbPos = std::make_unique<Buffer>(
            dev, Buffer::Desc{
                   .name        = "Mesh vertices",
                   .usage       = ResourceUsage::ShaderResource,
                   .size        = vertices.size() * sizeof(Float4),
                   .defaultView = ViewType::SRV,
                   .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbPos, vertices);
          primitive.param->data.vbPosIdx = static_cast<i32>(
            primitive.vbPos->GetView(ViewType::SRV)->binding.index);

          if(prim.indicesAccessorIndex) {
            Arr<u32> indices;

            const auto& acc =
              data.accessors[*prim.indicesAccessorIndex];
            for(u32 i = 0; i < acc.count; i++) {
              indices.push_back(readIndex(data, acc, i));
            }

            primitive.indices = std::make_unique<Buffer>(
              dev, Buffer::Desc{
                     .name       = "Model indices",
                     .usage      = ResourceUsage::IndexBuffer,
                     .size       = indices.size() * sizeof(u32),
                     .memoryType = MemoryType::Main});

            ctx.Write(
              *primitive.indices, indices, ResourceState::IndexBuffer);
            primitive.count = indices.size();
          } else {
            primitive.count = vertices.size();
          }
        }

        if(attr.type == VertexAttribute::TexCoord) {
          Arr<u32>
            uv0; // two half floats packed into one u32

          const auto& acc    = data.accessors[attr.accessorIndex];
          for(u32 i = 0; i < acc.count; i++) {
            const auto texCoord = readVector(
              data, acc, i, data::AccessorType::Vector2);
            const f32 u = texCoord[0];
            const f32 v = texCoord[1];

            // Pack two f32s into half floats in a single u32
            u16 uHalf  = mt::Float32ToFloat16(u);
            u16 vHalf  = mt::Float32ToFloat16(v);
            u32 packed = (u32)uHalf | ((u32)vHalf << 16);

            uv0.push_back(packed);
          }

          primitive.vbUV0 = std::make_unique<Buffer>(
            dev,
            Buffer::Desc{
              .name  = "Mesh texture coordinates",
              .usage = ResourceUsage::ShaderResource,
              .size  = uv0.size() *
                      sizeof(u32),
              .defaultView = ViewType::SRV,
              .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbUV0, uv0);
          primitive.param->data.vbUV0Idx = static_cast<i32>(
            primitive.vbUV0->GetView(ViewType::SRV)->binding.index);
        }

        if(attr.type == VertexAttribute::Normal) {
          Arr<u32> normals;

          const auto& acc    = data.accessors[attr.accessorIndex];
          for(u32 i = 0; i < acc.count; i++) {
            const auto normal = readVector(
              data, acc, i, data::AccessorType::Vector3);
            const f32 x = normal[0];
            const f32 y = normal[1];
            const f32 z = normal[2];

            // Pack normalized components into bytes
            u32 packed = 0u;
            packed |= packSignedUnit(x) << 0u;
            packed |= packSignedUnit(y) << 8u;
            packed |= packSignedUnit(z) << 16u;

            normals.push_back(packed);
          }

          primitive.vbNrm = std::make_unique<Buffer>(
            dev, Buffer::Desc{
                   .name        = "Mesh normals",
                   .usage       = ResourceUsage::ShaderResource,
                   .size        = normals.size() * sizeof(u32),
                   .defaultView = ViewType::SRV,
                   .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbNrm, normals);
          primitive.param->data.vbNrmIdx = static_cast<i32>(
            primitive.vbNrm->GetView(ViewType::SRV)->binding.index);
        }

        if(attr.type == VertexAttribute::Tangent) {
          Arr<u32> tangents;

          const auto& acc    = data.accessors[attr.accessorIndex];
          for(u32 i = 0; i < acc.count; i++) {
            const auto tangent = readVector(
              data, acc, i, data::AccessorType::Vector4);
            const f32 x = tangent[0];
            const f32 y = tangent[1];
            const f32 z = tangent[2];
            const f32 w = tangent[3];

            // Pack normalized components into bytes
            u32 packed = 0u;
            packed |= packSignedUnit(x) << 0u;
            packed |= packSignedUnit(y) << 8u;
            packed |= packSignedUnit(z) << 16u;
            packed |= packSignedUnit(w) << 24u;

            tangents.push_back(packed);
          }

          primitive.vbTan = std::make_unique<Buffer>(
            dev, Buffer::Desc{
                   .name        = "Mesh tangents",
                   .usage       = ResourceUsage::ShaderResource,
                   .size        = tangents.size() * sizeof(u32),
                   .defaultView = ViewType::SRV,
                   .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbTan, tangents);
          primitive.param->data.vbTanIdx = static_cast<i32>(
            primitive.vbTan->GetView(ViewType::SRV)->binding.index);
        }
      }

      primitive.param->Update();
      mesh.primitives.push_back(std::move(primitive));
    }

    model.meshes.push_back(std::move(mesh));
  }

  models.push_back(std::move(model));
}

void Scene::bindIblToFrames()
{
  const i32 irrIdx =
    irradianceMap
      ? static_cast<i32>(
          irradianceMap->GetView(ViewType::SRV)->binding.index)
      : -1;
  const i32 prefIdx =
    prefilteredMap
      ? static_cast<i32>(
          prefilteredMap->GetView(ViewType::SRV)->binding.index)
      : -1;
  const i32 lutIdx =
    brdfLut
      ? static_cast<i32>(brdfLut->GetView(ViewType::SRV)->binding.index)
      : -1;
  const i32 envIdx =
    environmentMap
      ? static_cast<i32>(
          environmentMap->GetView(ViewType::SRV)->binding.index)
      : -1;

  for(auto& frame: frames) {
    if(!frame.scene) continue;
    frame.scene->data.irradianceMapIdx    = irrIdx;
    frame.scene->data.prefilteredMapIdx   = prefIdx;
    frame.scene->data.brdfLutIdx          = lutIdx;
    frame.scene->data.prefilteredMipCount = prefilteredMipCount;
    frame.scene->data.environmentMapIdx   = envIdx;
    frame.scene->Update();
  }
}

void Scene::loadEnvironment(RenderContext& ctx, const Str& path)
{
  auto& dev = ctx.GetDevice();
  ctx.Reset();

  HdrImage env;
  if(path.empty()) {
    env = makeProceduralSky(256u, 128u);
  } else {
    DataReader reader;
    auto       img = reader.ReadImage(path, {.uri = path});
    if(
      img.format != Format::R32G32B32A32_SFLOAT || img.texels.empty()) {
      throw std::runtime_error(
        "loadEnvironment: expected HDR float RGBA image");
    }
    env.width  = img.size[0];
    env.height = img.size[1];
    env.rgba.resize(img.texels.size() / sizeof(f32));
    std::memcpy(env.rgba.data(), img.texels.data(), img.texels.size());
  }

  // Keep the full-resolution panorama for the background; the convolution
  // below uses a downscaled copy.
  environmentMap = std::make_unique<Image>(
    dev, Image::Desc{
           .name        = "IBL environment",
           .usage       = ResourceUsage::ShaderResource,
           .format      = Format::R32G32B32A32_SFLOAT,
           .dimension   = Dimension::e2D,
           .extent      = {env.width, env.height, 1u},
           .defaultView = ViewType::SRV,
           .mips        = 1u});
  ctx.Write(*environmentMap, hdrToBytes(env));

  // Work at a modest resolution for CPU convolution.
  if(env.width > 256u || env.height > 128u) {
    env = resizeEquirect(env, 256u, 128u);
  }

  // Low sample counts, enough for the sample.
  constexpr u32 kIrrW = 32u, kIrrH = 16u, kIrrSamples = 64u;
  constexpr u32 kPrefW = 128u, kPrefH = 64u, kPrefSamples = 32u;
  constexpr u32 kLutSize = 128u, kLutSamples = 64u;
  // R32F pitch-safe mips: 128,64,32,16 (16*16B = 256B row pitch).
  constexpr u32 kPrefMipCount = 4u;

  auto irradiance = convolveIrradiance(env, kIrrW, kIrrH, kIrrSamples);

  Arr<HdrImage> prefMips;
  prefMips.reserve(kPrefMipCount);
  u32 mw = kPrefW, mh = kPrefH;
  for(u32 mip = 0; mip < kPrefMipCount; ++mip) {
    const f32 roughness =
      static_cast<f32>(mip) / static_cast<f32>(kPrefMipCount - 1u);
    prefMips.push_back(
      prefilterMip(env, mw, mh, roughness, kPrefSamples));
    mw = std::max(1u, mw / 2u);
    mh = std::max(1u, mh / 2u);
  }

  auto lut = generateBrdfLut(kLutSize, kLutSamples);

  irradianceMap = std::make_unique<Image>(
    dev, Image::Desc{
           .name        = "IBL irradiance",
           .usage       = ResourceUsage::ShaderResource,
           .format      = Format::R32G32B32A32_SFLOAT,
           .dimension   = Dimension::e2D,
           .extent      = {kIrrW, kIrrH, 1u},
           .defaultView = ViewType::SRV,
           .mips        = 1u});
  ctx.Write(*irradianceMap, hdrToBytes(irradiance));

  prefilteredMap = std::make_unique<Image>(
    dev, Image::Desc{
           .name        = "IBL prefiltered",
           .usage       = ResourceUsage::ShaderResource,
           .format      = Format::R32G32B32A32_SFLOAT,
           .dimension   = Dimension::e2D,
           .extent      = {kPrefW, kPrefH, 1u},
           .defaultView = ViewType::SRV,
           .mips        = kPrefMipCount});
  ctx.Write(*prefilteredMap, buildMipChainBytes(prefMips));

  brdfLut = std::make_unique<Image>(
    dev, Image::Desc{
           .name        = "IBL BRDF LUT",
           .usage       = ResourceUsage::ShaderResource,
           .format      = Format::R32G32B32A32_SFLOAT,
           .dimension   = Dimension::e2D,
           .extent      = {kLutSize, kLutSize, 1u},
           .defaultView = ViewType::SRV,
           .mips        = 1u});
  ctx.Write(*brdfLut, hdrToBytes(lut));

  prefilteredMipCount = static_cast<f32>(kPrefMipCount);
  bindIblToFrames();
}

void Scene::loadCubeModel(RenderContext& ctx)
{
  auto& dev = ctx.GetDevice();
  ctx.Reset();

  Model model;
  Mesh  mesh;
  Prim  primitive;
  model.name              = "Cube";
  model.source            = "procedural";
  model.materialNames     = {"Default"};
  mesh.name               = "Cube";
  primitive.materialIndex = 0u;
  primitive.materialName  = "Default";

  auto material = std::make_unique<UniformBuffer<MaterialParam>>(
    dev, Buffer::Desc{
           .name        = "Cube material",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(MaterialParam),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Upload});

  material->data = {
    .colIdx          = -1,
    .nrmIdx          = -1,
    .mrIdx           = -1,
    .aoIdx           = -1,
    .emissiveIdx     = -1,
    .metallic        = 0.f,
    .roughness       = 0.5f,
    .aoStrength      = 1.f,
    .baseColorFactor = {0.8f, 0.8f, 0.8f, 1.f},
    .emissiveFactor  = {0.f, 0.f, 0.f},
  };
  material->Update();
  model.materials.push_back(std::move(material));

  Float4 vertices[] = {
    {-1.0f, -1.0f, -1.0f, .0f}, {-1.0f, -1.0f, 1.0f, .0f},
    {-1.0f, 1.0f, 1.0f, .0f},   {-1.0f, 1.0f, 1.0f, .0f},
    {-1.0f, 1.0f, -1.0f, .0f},  {-1.0f, -1.0f, -1.0f, .0f},

    {-1.0f, -1.0f, -1.0f, .0f}, {1.0f, 1.0f, -1.0f, .0f},
    {1.0f, -1.0f, -1.0f, .0f},  {-1.0f, -1.0f, -1.0f, .0f},
    {-1.0f, 1.0f, -1.0f, .0f},  {1.0f, 1.0f, -1.0f, .0f},

    {-1.0f, -1.0f, -1.0f, .0f}, {1.0f, -1.0f, -1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {-1.0f, -1.0f, -1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {-1.0f, -1.0f, 1.0f, .0f},

    {-1.0f, 1.0f, -1.0f, .0f},  {-1.0f, 1.0f, 1.0f, .0f},
    {1.0f, 1.0f, 1.0f, .0f},    {-1.0f, 1.0f, -1.0f, .0f},
    {1.0f, 1.0f, 1.0f, .0f},    {1.0f, 1.0f, -1.0f, .0f},

    {1.0f, 1.0f, -1.0f, .0f},   {1.0f, 1.0f, 1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {1.0f, -1.0f, 1.0f, .0f},
    {1.0f, -1.0f, -1.0f, .0f},  {1.0f, 1.0f, -1.0f, .0f},

    {-1.0f, 1.0f, 1.0f, .0f},   {-1.0f, -1.0f, 1.0f, .0f},
    {1.0f, 1.0f, 1.0f, .0f},    {-1.0f, -1.0f, 1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {1.0f, 1.0f, 1.0f, .0f},
  };

  // Positions are points: w must be 1 so the view translation applies.
  for(auto& vertex: vertices) vertex[3] = 1.f;

  const auto packNormal = [](f32 x, f32 y, f32 z) {
    const auto component = [](f32 value) {
      return static_cast<u32>((value * 0.5f + 0.5f) * 255.f);
    };
    return component(x) | (component(y) << 8u) | (component(z) << 16u);
  };
  const SArr<u32, 6> faceNormals{
    packNormal(-1.f, 0.f, 0.f), packNormal(0.f, 0.f, -1.f),
    packNormal(0.f, -1.f, 0.f), packNormal(0.f, 1.f, 0.f),
    packNormal(1.f, 0.f, 0.f),  packNormal(0.f, 0.f, 1.f)};
  SArr<u32, 36> normals{};
  for(size_t face = 0; face < faceNormals.size(); ++face) {
    for(size_t vertex = 0; vertex < 6u; ++vertex)
      normals[face * 6u + vertex] = faceNormals[face];
  }

  primitive.vbPos = std::make_unique<Buffer>(
    dev, Buffer::Desc{
           .name        = "Cube vertices",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(vertices),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Main});

  ctx.Write(*primitive.vbPos, vertices);

  primitive.vbNrm = std::make_unique<Buffer>(
    dev, Buffer::Desc{
           .name        = "Cube normals",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(normals),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Main});
  ctx.Write(*primitive.vbNrm, normals);

  primitive.param = std::make_unique<UniformBuffer<PrimParam>>(
    dev, Buffer::Desc{
           .name        = "Cube parameters",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(PrimParam),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Upload});

  primitive.param->data = {
    .world    = mt::Identity<Float44>(),
    .vbPosIdx = static_cast<i32>(
      primitive.vbPos->GetView(ViewType::SRV)->binding.index),
    .vbNrmIdx = static_cast<i32>(
      primitive.vbNrm->GetView(ViewType::SRV)->binding.index),
    .vbTanIdx    = -1,
    .vbUV0Idx    = -1,
    .materialIdx = static_cast<i32>(model.materials[0]
                                      ->buffer->GetView(ViewType::SRV)
                                      ->binding.index)};
  primitive.param->Update();

  primitive.count       = 36;
  primitive.vertexCount = 36u;
  mesh.primitives.push_back(std::move(primitive));
  model.meshes.push_back(std::move(mesh));
  models.push_back(std::move(model));
}

bool Scene::render(
  RenderContext& ctx, const Viewport& viewport, const Rect& renderArea,
  Ui* ui)
{
  auto& dev = ctx.GetDevice();
  auto& sc  = dev.GetSwapchain();

  // Reset (waits the frame fence) before Acquire so the acquire semaphore
  // has no pending wait/signal (VUID-vkAcquireNextImageKHR-semaphore-01779).
  ctx.Reset();

  Image* backbuffer = sc.AcquireNextImage(true);
  if(!backbuffer) { return false; }

  const auto frameIdx = sc.GetFrameIndex();
  auto&      frame    = frames[frameIdx];

  auto&       c             = m_controls;
  const auto  now           = std::chrono::steady_clock::now();
  static auto previousFrame = now;
  const f32   deltaSeconds  = std::min(
    std::chrono::duration<f32>(now - previousFrame).count(), 0.1f);
  previousFrame = now;
  if(c.autoOrbit) c.orbitYaw += 0.35f * deltaSeconds;

  const f32  cp        = std::cos(c.orbitPitch);
  const f32  sp        = std::sin(c.orbitPitch);
  const f32  cy        = std::cos(c.orbitYaw);
  const f32  sy        = std::sin(c.orbitYaw);
  const auto cameraPos = Float3{
    c.orbitDistance * cp * cy, c.orbitDistance * sp,
    c.orbitDistance * cp * sy};
  const auto cameraTgt = Float3{0, 0, 0};
  const auto cameraUp  = Float3{0, 1, 0};
  const auto view      = mt::LookAt(cameraPos, cameraTgt, cameraUp);
  const auto aspect    = viewport.extent[1] > 0.f
                           ? viewport.extent[0] / viewport.extent[1]
                           : 1.f;
  const auto proj = mt::PerspectiveFov(c.fovY, aspect, 0.1f, 1000.f);

  frame.scene->data.view           = view;
  frame.scene->data.viewProjection = mt::Mul(view, proj);
  frame.scene->data.cameraPosition = cameraPos;
  frame.scene->data.cameraAspect   = aspect;
  frame.scene->data.gbufIdx0       = static_cast<i32>(
    frame.gbuffers[0]->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.gbufIdx1 = static_cast<i32>(
    frame.gbuffers[1]->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.gbufIdx2 = static_cast<i32>(
    frame.gbuffers[2]->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.gbufIdx3 = static_cast<i32>(
    frame.gbuffers[3]->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.ambientColor     = c.ambientColor;
  frame.scene->data.ambientIntensity = c.ambientIntensity;
  frame.scene->data.directionalLightDirection =
    mt::Norm(c.lightDirection);
  frame.scene->data.directionalLightColor     = c.lightColor;
  frame.scene->data.directionalLightIntensity = c.lightIntensity;
  frame.scene->data.iblIntensity              = c.iblIntensity;
  frame.scene->data.iblEnabled                = c.iblEnabled ? 1 : 0;
  frame.scene->data.iblRotationYaw            = c.iblRotationYaw;
  frame.scene->data.exposure                  = c.exposure;
  frame.scene->data.gamma                     = c.gamma;
  frame.scene->data.debugView         = static_cast<i32>(c.debugView);
  frame.scene->data.cameraTanHalfFovY = std::tan(c.fovY * 0.5f);
  // Keep IBL bindless indices fresh (prepare() clears them to -1).
  frame.scene->data.irradianceMapIdx =
    irradianceMap
      ? static_cast<i32>(
          irradianceMap->GetView(ViewType::SRV)->binding.index)
      : -1;
  frame.scene->data.prefilteredMapIdx =
    prefilteredMap
      ? static_cast<i32>(
          prefilteredMap->GetView(ViewType::SRV)->binding.index)
      : -1;
  frame.scene->data.brdfLutIdx =
    brdfLut
      ? static_cast<i32>(brdfLut->GetView(ViewType::SRV)->binding.index)
      : -1;
  frame.scene->data.prefilteredMipCount = prefilteredMipCount;
  frame.scene->data.environmentMapIdx =
    environmentMap
      ? static_cast<i32>(
          environmentMap->GetView(ViewType::SRV)->binding.index)
      : -1;
  {
    const auto extent = frame.sceneColor->GetDesc().extent;
    frame.scene->data.aaInvSize = {
      1.f / static_cast<f32>(extent[0]),
      1.f / static_cast<f32>(extent[1])};
    frame.scene->data.aaSize = {
      static_cast<f32>(extent[0]), static_cast<f32>(extent[1])};
  }
  frame.scene->data.sceneColorIdx = static_cast<i32>(
    frame.sceneColor->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.aaEdgesIdx = static_cast<i32>(
    frame.smaaEdges->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.aaBlendIdx = static_cast<i32>(
    frame.smaaBlend->GetView(ViewType::SRV)->binding.index);
  frame.scene->data.aaAreaIdx =
    smaaArea
      ? static_cast<i32>(smaaArea->GetView(ViewType::SRV)->binding.index)
      : -1;
  frame.scene->data.aaSearchIdx =
    smaaSearch ? static_cast<i32>(
                   smaaSearch->GetView(ViewType::SRV)->binding.index)
               : -1;
  frame.scene->Update();

  if(ui)
    ui->update(
      static_cast<u32>(viewport.extent[0]),
      static_cast<u32>(viewport.extent[1]), ctx.GetFrameIndex());

  auto& cmd = ctx.GetCmd(QueueType::Graphics);

  // Lighting writes LDR into sceneColor; UI writes the acquired backbuffer.
  frame.passes[PassLighting].color[0].view =
    frame.sceneColor->GetView(ViewType::RTV);
  frame.passes[PassUi].color[0].view = backbuffer->GetView();

  cmd.Begin();

  cmd.SetViewport(viewport);
  cmd.SetScissor(renderArea);

  for(u32 idx = 0; idx < GBufferCount; ++idx) {
    cmd.AddBarrier(*frame.gbuffers[idx], ResourceState::RenderTarget);
  }
  cmd.AddBarrier(*frame.depthStencil, ResourceState::DepthStencilRW);
  cmd.AddBarrier(*frame.sceneColor, ResourceState::RenderTarget);
  cmd.AddBarrier(*frame.smaaEdges, ResourceState::RenderTarget);
  cmd.AddBarrier(*frame.smaaBlend, ResourceState::RenderTarget);
  cmd.AddBarrier(*backbuffer, ResourceState::RenderTarget);
  if(irradianceMap) {
    cmd.AddBarrier(
      *irradianceMap, ResourceState::ShaderResourceGraphics);
  }
  if(prefilteredMap) {
    cmd.AddBarrier(
      *prefilteredMap, ResourceState::ShaderResourceGraphics);
  }
  if(environmentMap) {
    cmd.AddBarrier(
      *environmentMap, ResourceState::ShaderResourceGraphics);
  }
  if(brdfLut) {
    cmd.AddBarrier(*brdfLut, ResourceState::ShaderResourceGraphics);
  }
  if(smaaArea) {
    cmd.AddBarrier(*smaaArea, ResourceState::ShaderResourceGraphics);
  }
  if(smaaSearch) {
    cmd.AddBarrier(*smaaSearch, ResourceState::ShaderResourceGraphics);
  }

  cmd.FlushBarriers();

  // Execute geometry + lighting passes (not UI yet).
  for(auto& pass: frame.passes) {
    if(pass.id == PassUi) continue;

    pass.pipeline->Bind(cmd);

    if(pass.id == PassLighting) {
      // Lighting pass - fullscreen triangle into LDR sceneColor
      SArr<u32, 4> push{
        frame.scene->buffer->GetView(ViewType::SRV)->binding.index, 0u,
        0u, 0u};
      cmd.PushConstants(push);

      cmd.BeginRendering(pass.color, &pass.depthStencil);
      cmd.Draw(3u);
      cmd.EndRendering();
    } else {
      // Geometry passes - draw all models, meshes and primitives
      bool texturesTransitioned = false;
      for(auto& model: models) {
        for(auto& tex: model.textures) {
          cmd.AddBarrier(*tex, ResourceState::ShaderResourceGraphics);
          texturesTransitioned = true;
        }
      }
      if(texturesTransitioned) cmd.FlushBarriers();

      // Begin even for an empty scene so LoadOp::Clear runs. Outside the
      // primitive loop so later primitives do not clear earlier ones.
      cmd.BeginRendering(pass.color, &pass.depthStencil);

      for(auto& model: models) {
        for(auto& mesh: model.meshes) {
          for(auto& prim: mesh.primitives) {
            SArr<u32, 4> push{
              frame.scene->buffer->GetView(ViewType::SRV)
                ->binding.index,
              prim.param->buffer->GetView(ViewType::SRV)->binding.index,
              0u, 0u};
            cmd.PushConstants(push);

            if(prim.indices) {
              cmd.BindIndexBuffer(*prim.indices, IndexType::U32);
              cmd.DrawIndexed(prim.count);
            } else {
              cmd.Draw(prim.count);
            }
          }
        }
      }
      cmd.EndRendering();
    }

    if(pass.id == PassDepth) {
      cmd.AddBarrier(
        *frame.depthStencil, ResourceState::DepthStencilRO);
      cmd.FlushBarriers();
    }

    if(pass.id == PassGBuffer) {
      for(u32 idx = 0; idx < GBufferCount; ++idx) {
        cmd.AddBarrier(
          *frame.gbuffers[idx], ResourceState::ShaderResourceGraphics);
      }
      cmd.FlushBarriers();
    }

    if(pass.id == PassLighting) {
      cmd.AddBarrier(
        *frame.sceneColor, ResourceState::ShaderResourceGraphics);
      cmd.FlushBarriers();
    }
  }

  // Post-tonemap AA (or resolve) into the swapchain, before UI.
  const auto drawFullscreen = [&](Pipeline& pipeline,
                                  const CommandBuffer::Attachment& color) {
    pipeline.Bind(cmd);
    SArr<u32, 4> push{
      frame.scene->buffer->GetView(ViewType::SRV)->binding.index, 0u, 0u,
      0u};
    cmd.PushConstants(push);
    Arr<CommandBuffer::Attachment> colors{color};
    cmd.BeginRendering(colors);
    cmd.Draw(3u);
    cmd.EndRendering();
  };

  CommandBuffer::Attachment swapColor{
    .view    = backbuffer->GetView(),
    .state   = ResourceState::RenderTarget,
    .loadOp  = LoadOp::Clear,
    .storeOp = StoreOp::Store,
  };

  switch(c.aaMode) {
    case AaMode::Fxaa:
      drawFullscreen(
        *aaPipelines[static_cast<size_t>(AaPipeline::Fxaa)], swapColor);
      break;
    case AaMode::Smaa: {
      CommandBuffer::Attachment edgeColor{
        .view    = frame.smaaEdges->GetView(ViewType::RTV),
        .state   = ResourceState::RenderTarget,
        .loadOp  = LoadOp::Clear,
        .storeOp = StoreOp::Store,
        .clearValue = Float4{0.f, 0.f, 0.f, 0.f},
      };
      drawFullscreen(
        *aaPipelines[static_cast<size_t>(AaPipeline::SmaaEdge)],
        edgeColor);
      cmd.AddBarrier(
        *frame.smaaEdges, ResourceState::ShaderResourceGraphics);
      cmd.FlushBarriers();

      CommandBuffer::Attachment weightColor{
        .view    = frame.smaaBlend->GetView(ViewType::RTV),
        .state   = ResourceState::RenderTarget,
        .loadOp  = LoadOp::Clear,
        .storeOp = StoreOp::Store,
        .clearValue = Float4{0.f, 0.f, 0.f, 0.f},
      };
      drawFullscreen(
        *aaPipelines[static_cast<size_t>(AaPipeline::SmaaWeights)],
        weightColor);
      cmd.AddBarrier(
        *frame.smaaBlend, ResourceState::ShaderResourceGraphics);
      cmd.FlushBarriers();

      drawFullscreen(
        *aaPipelines[static_cast<size_t>(AaPipeline::SmaaBlend)],
        swapColor);
      break;
    }
    case AaMode::Off:
    default:
      drawFullscreen(
        *aaPipelines[static_cast<size_t>(AaPipeline::Resolve)],
        swapColor);
      break;
  }

  // Draw RmlUi's persistent indexed geometry directly onto the backbuffer.
  if(ui) {
    auto& uiPass = frame.passes[PassUi];
    uiPass.pipeline->Bind(cmd);
    cmd.BeginRendering(uiPass.color);
    ui->render(cmd, renderArea);
    cmd.EndRendering();
  }

  // Transition backbuffer for present
  cmd.AddBarrier(*backbuffer, ResourceState::Present);
  cmd.FlushBarriers();

  cmd.End();
  ctx.Submit({&cmd}, {}, {}, SwapchainDep::AcquireRelease);
  sc.Present();
  sc.NextFrame();
  return !sc.NeedsRecreate();
}

} // namespace vd
