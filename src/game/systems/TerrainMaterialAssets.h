#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

namespace game::systems {

inline DirectX::XMFLOAT3 TerrainMaterialMapColor(std::uint8_t material) {
  switch (material) {
  case 0: return {0.25f, 0.43f, 0.16f};
  case 1: return {0.18f, 0.32f, 0.11f};
  case 2: return {0.90f, 0.85f, 0.70f};
  case 3: return {0.30f, 0.52f, 0.19f};
  case 4: return {0.70f, 0.88f, 0.98f};
  case 5: return {0.20f, 0.45f, 0.85f};
  case 6: return {0.95f, 0.35f, 0.12f};
  case 7: return {0.50f, 0.48f, 0.52f};
  default: return {1.0f, 1.0f, 1.0f};
  }
}

inline const std::vector<std::string> &TerrainAlbedoTexturePaths() {
  static const std::vector<std::string> paths = {
      "Assets/textures/terrain_materials/terrain_00_fairway_albedo.png",
      "Assets/textures/terrain_materials/terrain_01_rough_albedo.png",
      "Assets/textures/terrain_materials/terrain_02_bunker_albedo.png",
      "Assets/textures/terrain_materials/terrain_03_green_albedo.png",
      "Assets/textures/terrain_materials/terrain_04_ice_albedo.png",
      "Assets/textures/terrain_materials/terrain_05_water_albedo.png",
      "Assets/textures/terrain_materials/terrain_06_lava_albedo.png",
      "Assets/textures/terrain_materials/terrain_07_stone_albedo.png"};
  return paths;
}

inline const std::vector<std::string> &TerrainNormalTexturePaths() {
  static const std::vector<std::string> paths = {
      "Assets/textures/terrain_materials/terrain_00_fairway_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_01_rough_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_02_bunker_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_03_green_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_04_ice_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_05_water_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_06_lava_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_07_stone_normal_dx.png"};
  return paths;
}

} // namespace game::systems
