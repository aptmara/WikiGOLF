/**
 * @file AimPinController.cpp
 * @brief AimPinController の実装
*/

#include "AimPinController.h"
#include "../../core/Input.h"
#include "../../ecs/World.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../utils/ProceduralFlag.h"
#include "../utils/ScreenRaycast.h"
#include <cmath>
#include <utility>

#ifdef WIKIGOLF_DEBUG_TOOLS
#include "../devtools/DebugRaycastState.h"
#endif

namespace game::controllers {

namespace {
constexpr float kClickMoveThreshold = 6.0f; ///< これ以上動いたらドラッグ扱い
/** @brief エイムピン旗の色。ホール旗の配色(赤=目的地/黄橙=近距離/白灰=遠距離/青=未解析)
 * と混同しないよう、それらに含まれないマゼンタを使う。*/
const DirectX::XMFLOAT4 kAimPinFlagColor = {1.0f, 0.2f, 0.9f, 1.0f};
} // namespace

AimPinController::UpdateResult
AimPinController::Update(core::GameContext &ctx, const UpdateParams &params) {
  UpdateResult result;

  if (!params.allowInput) {
    m_middleButtonDown = false;
    return result;
  }

  if (ctx.input.GetMouseButtonDown(2)) {
    m_middleButtonDown = true;
    m_pressX = params.mouseX;
    m_pressY = params.mouseY;
  }

  if (!ctx.input.GetMouseButtonUp(2)) {
    return result;
  }
  const bool wasTrackedPress = m_middleButtonDown;
  m_middleButtonDown = false;
  if (!wasTrackedPress) {
    return result; // 入力不可の間に押されたクリックは無視
  }

  const float dx = static_cast<float>(params.mouseX - m_pressX);
  const float dy = static_cast<float>(params.mouseY - m_pressY);
  if (dx * dx + dy * dy > kClickMoveThreshold * kClickMoveThreshold) {
    return result; // マップパンなどのドラッグ操作なのでピンは置かない
  }

  auto *ballTransform =
      ctx.world.Get<game::components::Transform>(params.ballEntity);
  if (!ballTransform) {
    return result;
  }

  // 全体マップビュー中もメインカメラ自体が高所へ移動しているだけの実カメラ
  // (透視投影)なので、三人称視点と同じレイキャストがそのまま使える。
  // 探索距離の上限(maxDistance)は呼び出し側が視点モードに応じて決める
  // （三人称は近距離のみ、マップビューはフィールド全体をカバーする等）。
  DirectX::XMFLOAT3 worldPos{0.0f, 0.0f, 0.0f};
#ifdef WIKIGOLF_DEBUG_TOOLS
  // デバッグツールビルドのみ: レイの始点/方向も取得し、デバッグオーバーレイ
  // (WikiGOLF デバッグ [F1] > 衝突 タブ)でこのレイを可視化できるように
  // グローバル状態へ書き込む。可視化自体のON/OFFはオーバーレイ側のチェック
  // ボックスで行うため、ここでは常に最新の結果を残すだけでよい。
  DirectX::XMFLOAT3 debugRayOrigin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 debugRayDirection{0.0f, 0.0f, 0.0f};
  const bool hit = game::utils::RaycastScreenToTerrain(
      ctx, params.cameraEntity, static_cast<float>(params.mouseX),
      static_cast<float>(params.mouseY), params.terrainSystem,
      params.maxDistance, worldPos, &debugRayOrigin, &debugRayDirection);

  {
    game::debug::DebugRaycastState debugState;
    debugState.hasRay = true;
    debugState.origin = debugRayOrigin;
    debugState.direction = debugRayDirection;
    debugState.hit = hit;
    debugState.hitPosition = worldPos;
    debugState.maxDistance = params.maxDistance;
    ctx.world.SetGlobal(std::move(debugState));
  }
#else
  const bool hit = game::utils::RaycastScreenToTerrain(
      ctx, params.cameraEntity, static_cast<float>(params.mouseX),
      static_cast<float>(params.mouseY), params.terrainSystem,
      params.maxDistance, worldPos);
#endif

  if (!hit) {
    return result;
  }

  const float distance = PlacePin(ctx, params.ballEntity, worldPos);
  if (distance < 0.0f) return result;

  result.pinPlaced = true;
  result.distance = distance;
  return result;
}

float AimPinController::PlacePin(core::GameContext& ctx,
                                 ecs::Entity ballEntity,
                                 const DirectX::XMFLOAT3& worldPos) {
  auto* ball = ctx.world.Get<game::components::Transform>(ballEntity);
  auto* pin = ctx.world.GetGlobal<game::components::AimPinState>();
  if (!ball || !pin) return -1.0f;

  const float dx = worldPos.x - ball->position.x;
  const float dz = worldPos.z - ball->position.z;
  const float distance = std::sqrt(dx * dx + dz * dz);
  pin->active = true;
  pin->worldPosition = worldPos;
  pin->distanceFromBall = distance;
  RebuildWorldMarker(ctx, worldPos);
  return distance;
}

void AimPinController::ClearPin(core::GameContext &ctx) {
  if (auto *pin = ctx.world.GetGlobal<game::components::AimPinState>()) {
    pin->active = false;
  }
  m_markerOwner.DestroyAll(ctx.world);
}

void AimPinController::Shutdown(core::GameContext &ctx) {
  m_markerOwner.DestroyAll(ctx.world);
}

void AimPinController::RebuildWorldMarker(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &worldPos) {
  m_markerOwner.DestroyAll(ctx.world);

  game::utils::ProceduralFlagOptions options;
  options.holeEntity = UINT32_MAX; // ホールに紐づかない説明用の旗として生成
  options.createParticles = false;
  options.large = false;

  const DirectX::XMFLOAT3 basePosition{worldPos.x, worldPos.y + 0.05f,
                                       worldPos.z};
  auto result = game::utils::CreateProceduralFlag(ctx, basePosition,
                                                   kAimPinFlagColor, options);
  for (auto entity : result.allEntities) {
    m_markerOwner.Track(entity);
  }
}

} // namespace game::controllers
