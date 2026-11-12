/**
 * @file ShotGuideView.cpp
 * @brief ショット予測表示の描画部品が共通で使う、板の生成と配置
*/

#include "ShotGuideView.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/MeshRenderer.h"
#include "../../components/Transform.h"
#include <cmath>

namespace game::guide {

using namespace DirectX;
using game::components::MeshRenderer;
using game::components::Transform;

ecs::Entity CreateGuideQuad(core::GameContext &ctx, ecs::EntityOwner &owner,
                            float shapeId, bool inFront) {
  const ecs::Entity entity = owner.Create(ctx.world);
  ctx.world.Add<Transform>(entity);

  auto &renderer = ctx.world.Add<MeshRenderer>(entity);
  renderer.mesh = ctx.resource.LoadMesh("builtin/quad");
  renderer.shader =
      ctx.resource.LoadShader("ShotGuide", L"Assets/shaders/ShotGuideVS.hlsl",
                              L"Assets/shaders/ShotGuidePS.hlsl");
  renderer.isTransparent = true;
  renderer.blendMode = game::components::BlendMode::Alpha;
  renderer.customFlags = {shapeId, 0.0f, 0.0f, 0.0f};
  renderer.drawsInFront = inFront;
  // 遠くの着地点も、高い所から見下ろす全体マップでも描く
  renderer.keepsVisibleAtDistance = true;
  // シェーダーが画面上の最小の大きさまで広げるので、カリングの球も広めに取る
  renderer.boundsScale = 8.0f;
  renderer.isVisible = false;
  return entity;
}

void HideGuideQuad(core::GameContext &ctx, ecs::Entity entity) {
  if (auto *renderer = ctx.world.Get<MeshRenderer>(entity)) {
    renderer->isVisible = false;
  }
}

void PlaceGroundQuad(core::GameContext &ctx, ecs::Entity entity,
                     const XMFLOAT3 &position, const XMFLOAT3 &normal,
                     const XMFLOAT3 &forward, float spin, float size,
                     const GuideColor &color, float shapeValue,
                     float sizeForward) {
  auto *transform = ctx.world.Get<Transform>(entity);
  auto *renderer = ctx.world.Get<MeshRenderer>(entity);
  if (!transform || !renderer) {
    return;
  }
  if (color.a <= 0.004f || size <= 0.0001f) {
    renderer->isVisible = false;
    return;
  }

  XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&normal));
  XMVECTOR front = XMVectorSet(forward.x, 0.0f, forward.z, 0.0f);
  if (XMVectorGetX(XMVector3LengthSq(front)) < 1.0e-8f) {
    front = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
  }
  // 「前」を地面の面内へ倒してから、面の向きを軸に回す
  front = XMVectorSubtract(front,
                           XMVectorScale(up, XMVectorGetX(XMVector3Dot(front, up))));
  if (XMVectorGetX(XMVector3LengthSq(front)) < 1.0e-8f) {
    front = XMVector3Cross(up, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
  }
  front = XMVector3Normalize(front);
  if (spin != 0.0f) {
    front = XMVector3Rotate(front, XMQuaternionRotationAxis(up, spin));
  }
  const XMVECTOR right = XMVector3Normalize(XMVector3Cross(front, up));

  // 板は XY 平面。X=右、Y=前、Z=地面の向き
  XMMATRIX basis = XMMatrixIdentity();
  basis.r[0] = XMVectorSetW(right, 0.0f);
  basis.r[1] = XMVectorSetW(front, 0.0f);
  basis.r[2] = XMVectorSetW(up, 0.0f);
  XMStoreFloat4(&transform->rotation, XMQuaternionRotationMatrix(basis));
  transform->position = position;
  transform->scale = {size, sizeForward > 0.0f ? sizeForward : size, size};

  renderer->color = color.ToFloat4();
  renderer->customFlags.y = shapeValue;
  renderer->isVisible = true;
}

bool PlaceRibbonQuad(core::GameContext &ctx, ecs::Entity entity,
                     const XMFLOAT3 &from, const XMFLOAT3 &to, float width,
                     const GuideColor &color, float shapeId, float shapeValue) {
  auto *transform = ctx.world.Get<Transform>(entity);
  auto *renderer = ctx.world.Get<MeshRenderer>(entity);
  if (!transform || !renderer) {
    return false;
  }
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float dz = to.z - from.z;
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (length < 1.0e-4f || color.a <= 0.004f) {
    renderer->isVisible = false;
    return false;
  }

  transform->position = {(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f,
                         (from.z + to.z) * 0.5f};
  // ローカルZ軸を from→to へ向ける（シェーダーは Z 軸を帯の長さ方向として読む）
  const float horizontal = std::sqrt(dx * dx + dz * dz);
  const float yaw = horizontal > 1.0e-6f ? std::atan2(dx, dz) : 0.0f;
  const float pitch = -std::atan2(dy, horizontal);
  XMStoreFloat4(&transform->rotation,
                XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0f));
  transform->scale = {width, width, length};

  renderer->color = color.ToFloat4();
  renderer->customFlags.x = shapeId;
  renderer->customFlags.y = shapeValue;
  renderer->isVisible = true;
  return true;
}

} // namespace game::guide
