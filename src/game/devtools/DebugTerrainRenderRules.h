#pragma once

namespace game::debug {

inline bool ShouldHideTerrainMesh(bool hideTerrainMeshes,
                                  bool isTerrainObject) {
  return hideTerrainMeshes && isTerrainObject;
}

} // namespace game::debug
