#pragma once

#include <string>
#include <vector>

namespace game::systems {

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
