#include <catch2/catch_test_macros.hpp>

#include "vuldir/DataReader.hpp"

using namespace vd;

namespace {

data::Model readModel(Strv json)
{
  DataReader reader(DataReader::Desc{
    .fileReader = [](const fs::path&, const fs::path*) {
      return Arr<u8>(16u, 0u);
    }});
  std::istringstream stream{Str(json)};
  return reader.ReadModel(stream, {});
}

} // namespace

TEST_CASE("glTF validates buffer and accessor byte ranges", "[core][gltf]")
{
  const auto model = readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteOffset":4,"byteLength":8}],
    "accessors":[{
      "bufferView":0,"byteOffset":0,"componentType":5126,
      "count":1,"type":"VEC2"
    }]
  })");
  REQUIRE(model.buffers[0].byteLength == 16u);
  REQUIRE(model.bufferViews[0].offset == 4u);
  REQUIRE(model.accessors[0].count == 1u);

  SECTION("buffer index") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "bufferViews":[{"buffer":0,"byteLength":4}]
    })"));
  }
  SECTION("negative buffer length") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"buffer.bin","byteLength":-1}]
    })"));
  }
  SECTION("buffer view range") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"buffer.bin","byteLength":16}],
      "bufferViews":[{"buffer":0,"byteOffset":15,"byteLength":2}]
    })"));
  }
  SECTION("accessor buffer view") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "accessors":[{
        "bufferView":0,"componentType":5126,"count":1,"type":"SCALAR"
      }]
    })"));
  }
  SECTION("accessor range") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"buffer.bin","byteLength":16}],
      "bufferViews":[{"buffer":0,"byteLength":4}],
      "accessors":[{
        "bufferView":0,"componentType":5126,"count":1,"type":"VEC2"
      }]
    })"));
  }
  SECTION("accessor alignment") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"buffer.bin","byteLength":16}],
      "bufferViews":[{"buffer":0,"byteOffset":1,"byteLength":8}],
      "accessors":[{
        "bufferView":0,"componentType":5126,"count":1,"type":"SCALAR"
      }]
    })"));
  }
}

TEST_CASE("glTF validates model reference indices", "[core][gltf]")
{
  SECTION("image buffer view") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "images":[{"bufferView":0,"mimeType":"image/png"}]
    })"));
  }
  SECTION("primitive accessor") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
    })"));
  }
  SECTION("primitive material") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "meshes":[{"primitives":[{"attributes":{},"material":0}]}]
    })"));
  }
  SECTION("texture image") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "textures":[{"source":0}]
    })"));
  }
  SECTION("texture sampler") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "textures":[{"sampler":0}]
    })"));
  }
  SECTION("material texture") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "materials":[{
        "emissiveFactor":[0,0,0],"normalTexture":{"index":0}
      }]
    })"));
  }
  SECTION("node mesh") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{"mesh":0}]
    })"));
  }
  SECTION("child node") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{"children":[1]}]
    })"));
  }
  SECTION("default scene") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "scene":1,"scenes":[{}]
    })"));
  }
  SECTION("scene root node") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "scenes":[{"nodes":[0]}]
    })"));
  }
}

TEST_CASE("glTF retains only supported vertex semantics", "[core][gltf]")
{
  const auto model = readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"},
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC2"},
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"}
    ],
    "meshes":[{"primitives":[{"attributes":{
      "POSITION":0,"TEXCOORD_2":1,"COLOR_1":2,
      "JOINTS_0":0,"WEIGHTS_0":0,"_CUSTOM":0
    }}]}]
  })");

  const auto& attributes = model.meshes[0].primitives[0].attributes;
  REQUIRE(attributes.size() == 3u);
  REQUIRE(attributes[0].type == VertexAttribute::Color);
  REQUIRE(attributes[0].typeIndex == 1u);
  REQUIRE(attributes[1].type == VertexAttribute::Position);
  REQUIRE(attributes[1].typeIndex == 0u);
  REQUIRE(attributes[2].type == VertexAttribute::TexCoord);
  REQUIRE(attributes[2].typeIndex == 2u);

  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "meshes":[{"primitives":[{"attributes":{"TEXCOORD_bad":0}}]}]
  })"));
}

TEST_CASE("glTF validates primitive accessor layouts", "[core][gltf]")
{
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC2"}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"},
      {"bufferView":0,"componentType":5126,"count":2,"type":"VEC2"}],
    "meshes":[{"primitives":[{"attributes":{
      "POSITION":0,"TEXCOORD_0":1
    }}]}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"},
      {"bufferView":0,"componentType":5126,"count":1,"type":"SCALAR"}],
    "meshes":[{"primitives":[{
      "attributes":{"POSITION":0},"indices":1
    }]}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"}],
    "meshes":[{"primitives":[{"attributes":{}}]}]
  })"));
}

TEST_CASE("glTF validates node hierarchies", "[core][gltf]")
{
  SECTION("cyclic hierarchy") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{"children":[1]},{"children":[0]}]
    })"));
  }
  SECTION("multiple parents") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{"children":[2]},{"children":[2]},{}]
    })"));
  }
  SECTION("invalid child type") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{"children":["one"]}]
    })"));
  }
  SECTION("invalid matrix length") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{"matrix":[1,0,0,1]}]
    })"));
  }
  SECTION("matrix and TRS") {
    REQUIRE_THROWS(readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{
        "matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],
        "translation":[0,0,0]
      }]
    })"));
  }
  SECTION("an explicit empty scene stays empty") {
    const auto model = readModel(R"({
      "asset":{"version":"2.0"},
      "nodes":[{}],"scenes":[{"nodes":[]}],"scene":0
    })");
    REQUIRE(model.rootNodes.empty());
  }
}

TEST_CASE("glTF requires a 2.0 asset declaration", "[core][gltf]")
{
  REQUIRE_THROWS(readModel(R"({"buffers":[]})"));
  REQUIRE_THROWS(readModel(R"({"asset":{"version":"1.0"}})"));
  REQUIRE_NOTHROW(readModel(R"({"asset":{"version":"2.0"}})"));
}

TEST_CASE("glTF rejects unsupported required features", "[core][gltf]")
{
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "extensionsUsed":["KHR_draco_mesh_compression"],
    "extensionsRequired":["KHR_draco_mesh_compression"]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "extensionsRequired":["KHR_mesh_quantization"]
  })"));
  REQUIRE_NOTHROW(readModel(R"({
    "asset":{"version":"2.0"},
    "extensionsUsed":["KHR_mesh_quantization"],
    "extensionsRequired":["KHR_mesh_quantization"]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":4}],
    "bufferViews":[{"buffer":0,"byteLength":4}],
    "accessors":[{
      "bufferView":0,"componentType":5126,"count":1,"type":"SCALAR",
      "sparse":{"count":1}
    }]
  })"));
}

TEST_CASE("glTF validates sampler and material values", "[core][gltf]")
{
  const auto defaults = readModel(R"({
    "asset":{"version":"2.0"},
    "samplers":[{}],"materials":[{}]
  })");
  REQUIRE(defaults.samplers[0].magFilter == SamplerFilter::Linear);
  REQUIRE(defaults.samplers[0].minFilter == SamplerFilter::Linear);
  REQUIRE(defaults.samplers[0].mipFilter == SamplerFilter::Linear);
  REQUIRE(defaults.samplers[0].useMipmaps);
  REQUIRE(std::holds_alternative<data::PbrMetallicRoughness>(
    defaults.materials[0].model));

  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},"samplers":[{"magFilter":1}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},"samplers":[{"magFilter":"LINEAR"}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},"samplers":[{"wrapS":1}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},"materials":[{"alphaMode":"OTHER"}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,1,1]}}]
  })"));
  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "materials":[{"pbrMetallicRoughness":{"roughnessFactor":2}}]
  })"));
}

TEST_CASE("glTF preserves valid accessor normalization", "[core][gltf]")
{
  const auto model = readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[{
      "bufferView":0,"componentType":5121,"count":1,
      "type":"VEC2","normalized":true
    }]
  })");
  REQUIRE(model.accessors[0].normalized);

  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[{
      "bufferView":0,"componentType":5121,"count":1,
      "type":"VEC2","min":[0]
    }]
  })"));

  REQUIRE_THROWS(readModel(R"({
    "asset":{"version":"2.0"},
    "buffers":[{"uri":"buffer.bin","byteLength":16}],
    "bufferViews":[{"buffer":0,"byteLength":16}],
    "accessors":[{
      "bufferView":0,"componentType":5126,"count":1,
      "type":"SCALAR","normalized":true
    }]
  })"));
}

TEST_CASE("PNG validates chunk checksums", "[core][png]")
{
  auto png = decodeBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4"
    "z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
  DataReader reader;
  const auto image = reader.ReadImage(Span<u8 const>{png}, {});
  REQUIRE(image.size[0] == 1u);
  REQUIRE(image.size[1] == 1u);
  REQUIRE(image.texels.size() == 4u);

  // Signature (8), length (4), type (4), IHDR payload (13), then CRC.
  png[29] ^= 1u;
  REQUIRE_THROWS(reader.ReadImage(Span<u8 const>{png}, {}));
}
