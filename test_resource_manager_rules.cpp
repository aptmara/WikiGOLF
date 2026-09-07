#include "src/graphics/GraphicsDevice.h"
#include "src/resources/ResourceManager.h"
#include <iostream>

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

  manager.Clear();
  manager.DumpStatistics();
  std::cout << "All ResourceManager rule tests passed.\n";
  return 0;
}
