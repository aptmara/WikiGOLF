#include "src/graphics/FbxLoader.h"
#include "src/graphics/GraphicsDevice.h"
#include "src/resources/ResourceManager.h"
#include <cmath>
#include <iostream>
#include <string>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                              \
      return 1;                                                               \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (false)

int main() {
  graphics::GraphicsDevice device;
  resources::ResourceManager manager(device);

  const resources::MeshHandle missingMesh = manager.FindMesh("missing");
  CHECK(!missingMesh.IsValid(), "Missing mesh returns an invalid handle");
  CHECK(manager.GetMesh(missingMesh) != nullptr,
        "Invalid mesh handle returns the release fallback resource");

  const resources::ShaderHandle missingShader = manager.FindShader("missing");
  CHECK(missingShader.index == 0 && missingShader.generation == 0,
        "Missing shader preserves the default invalid handle");
  CHECK(manager.GetShader(missingShader) == nullptr,
        "Default shader handle returns nullptr");
  CHECK(manager.GetAudio(resources::AudioHandle::Invalid()) != nullptr,
        "Invalid audio handle returns the release fallback resource");

  // 地球儀モデル: 各 LOD が半径 0.5・極軸 Y で、画像の上端が +Y の極に来る
  // （UV の上下が逆だとアイコンが南北逆さまに貼られる）。
  for (int lod = 0; lod < 3; ++lod) {
    const std::string path =
        "Assets/models/golf_puzzle_baked_lod" + std::to_string(lod) + ".glb";
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t> indices;
    CHECK(graphics::FbxLoader::Load(path, vertices, indices),
          "Globe LOD mesh loads: " + path);

    bool radiusOk = true;
    bool polesOk = true;
    for (const auto &v : vertices) {
      const float radius = std::sqrt(v.position.x * v.position.x +
                                     v.position.y * v.position.y +
                                     v.position.z * v.position.z);
      radiusOk = radiusOk && std::abs(radius - 0.5f) < 0.001f;
      if (v.position.y > 0.499f) {
        polesOk = polesOk && v.texCoord.y < 0.05f;
      }
      if (v.position.y < -0.499f) {
        polesOk = polesOk && v.texCoord.y > 0.95f;
      }
    }
    CHECK(radiusOk, "Globe LOD radius is 0.5 (kSkyGlobeModelRadius)");
    CHECK(polesOk, "Globe LOD texture top maps to the +Y pole");

    graphics::EmbeddedModelTextures textures;
    CHECK(graphics::FbxLoader::LoadEmbeddedTextures(path, textures),
          "Globe LOD embedded textures can be read");
    CHECK(!textures.baseColor.empty(), "Globe LOD embeds a base color texture");
    CHECK(!textures.normal.empty(), "Globe LOD embeds a normal map");
    CHECK(textures.baseColor != textures.normal,
          "Base color and normal map are distinct images");
  }

  manager.Clear();
  manager.DumpStatistics();
  std::cout << "All ResourceManager rule tests passed.\n";
  return 0;
}
