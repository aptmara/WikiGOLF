#pragma once

namespace core {
struct GameContext;
}

namespace game::debug {

struct DebugColliderSettings;

void DrawCupInInspector(core::GameContext &ctx,
                        DebugColliderSettings &colliderSettings);

} // namespace game::debug
