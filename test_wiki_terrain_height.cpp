/**
 * @file test_wiki_terrain_height.cpp
 * @brief WikiTerrainSystem の未生成時の高さ問い合わせを検証します。
 */

#include "src/game/systems/WikiTerrainSystem.h"

#include <cassert>

int main() {
  game::systems::WikiTerrainSystem terrain;
  assert(terrain.GetHeight(0.0f, 0.0f) == 0.0f);
  assert(terrain.GetHeight(-100.0f, 100.0f) == 0.0f);
  assert(terrain.GetEntities().empty());
  assert(terrain.GetFloorEntity() == 0xFFFFFFFF);
  assert(terrain.GetBuildProgress() == 0.0f);
  return 0;
}
