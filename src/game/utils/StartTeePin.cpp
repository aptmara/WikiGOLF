/**
 * @file StartTeePin.cpp
 * @brief スタートティー用ピン生成ユーティリティ実装
*/

#include "StartTeePin.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"

namespace game::utils {

using namespace DirectX;
using game::components::MeshRenderer;
using game::components::Transform;

namespace {
constexpr float kPinRadius = 0.045f;
} // namespace

StartTeePinResult CreateStartTeePin(core::GameContext &ctx,
                                    const XMFLOAT3 &groundPosition) {
  StartTeePinResult result;

  ecs::Entity entity = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(entity);
  t.position = {groundPosition.x, groundPosition.y + kStartTeePinHeight * 0.5f,
                groundPosition.z};
  t.scale = {kPinRadius * 2.0f, kStartTeePinHeight, kPinRadius * 2.0f};

  auto &mr = ctx.world.Add<MeshRenderer>(entity);
  mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {0.9f, 0.9f, 0.9f, 1.0f}; // タイトル画面のティーと同じ白
  mr.isVisible = true;

  result.entity = entity;
  return result;
}

} // namespace game::utils
