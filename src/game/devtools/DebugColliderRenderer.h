#pragma once

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
  bool holes = true;
  bool entityIds = false;
};

class DebugColliderRenderer {
public:
  void Draw(core::GameContext &ctx, const DebugColliderSettings &settings);
};

} // namespace game::debug
