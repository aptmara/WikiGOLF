#pragma once

#include "DebugBallTrailHistory.h"

namespace core {
struct GameContext;
}

namespace game::debug {

struct DebugColliderSettings {
  bool enabled = false;
  bool spheres = true;
  bool boxes = true;
  bool cylinders = true;
  bool terrain = true;
  bool terrainMaterials = false;
  bool holes = true;
  bool entityIds = false;
  bool contactPoints = true;
  bool collisionNormals = true;
  bool velocityVector = true;
  bool ballTrail = false;
  bool cupInGuide = false;
  bool raycast = false; ///< 中クリックのレイキャスト(エイムピン設置)を可視化するか
  int trailMaximumPoints = 300;
  int trailSampleInterval = 2;
  int trailClearGeneration = 0;
};

class DebugColliderRenderer {
public:
  void Draw(core::GameContext &ctx, const DebugColliderSettings &settings);

private:
  DebugBallTrailHistory m_ballTrail;
  int m_seenTrailClearGeneration = 0;
};

} // namespace game::debug
