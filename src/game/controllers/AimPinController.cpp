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

#ifdef _DEBUG
#include "../components/MeshRenderer.h"
#include <algorithm>
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

#ifdef _DEBUG
  // F9でレイキャスト可視化のON/OFFを切替（入力可否に関わらず常に受け付ける）。
  if (ctx.input.GetKeyDown(VK_F9)) {
    m_debugRaycastVizEnabled = !m_debugRaycastVizEnabled;
    SetDebugRayVisible(ctx, m_debugRaycastVizEnabled);
  }
#endif

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
#ifdef _DEBUG
  DirectX::XMFLOAT3 debugRayOrigin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 debugRayDirection{0.0f, 0.0f, 0.0f};
  const bool hit = game::utils::RaycastScreenToTerrain(
      ctx, params.cameraEntity, static_cast<float>(params.mouseX),
      static_cast<float>(params.mouseY), params.terrainSystem,
      params.maxDistance, worldPos, &debugRayOrigin, &debugRayDirection);

  if (m_debugRaycastVizEnabled) {
    UpdateDebugRayVisualization(ctx, debugRayOrigin, debugRayDirection, hit,
                                worldPos, params.maxDistance);
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

  auto *pin = ctx.world.GetGlobal<game::components::AimPinState>();
  if (!pin) {
    return result;
  }

  const float pdx = worldPos.x - ballTransform->position.x;
  const float pdz = worldPos.z - ballTransform->position.z;
  const float distance = std::sqrt(pdx * pdx + pdz * pdz);

  pin->active = true;
  pin->worldPosition = worldPos;
  pin->distanceFromBall = distance;
  RebuildWorldMarker(ctx, worldPos);

  result.pinPlaced = true;
  result.distance = distance;
  return result;
}

void AimPinController::ClearPin(core::GameContext &ctx) {
  if (auto *pin = ctx.world.GetGlobal<game::components::AimPinState>()) {
    pin->active = false;
  }
  m_markerOwner.DestroyAll(ctx.world);
}

void AimPinController::Shutdown(core::GameContext &ctx) {
  m_markerOwner.DestroyAll(ctx.world);
#ifdef _DEBUG
  m_debugRayOwner.DestroyAll(ctx.world);
  m_debugRayLineEntity = UINT32_MAX;
  m_debugRayHitMarkerEntity = UINT32_MAX;
#endif
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

#ifdef _DEBUG

namespace {
// ヒットしなかった場合、レイをどこまで伸ばして表示するか（ワールド単位）。
// maxDistance(最大3000)をそのまま使うと見づらいほど長くなるため上限を設ける。
constexpr float kDebugRayVisualMaxLength = 500.0f;
} // namespace

void AimPinController::EnsureDebugRayEntities(core::GameContext &ctx) {
  using namespace game::components;

  if (m_debugRayLineEntity != UINT32_MAX &&
      m_debugRayHitMarkerEntity != UINT32_MAX) {
    return;
  }

  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

  m_debugRayLineEntity = m_debugRayOwner.Create(ctx.world);
  ctx.world.Add<Transform>(m_debugRayLineEntity);
  auto &lineMr = ctx.world.Add<MeshRenderer>(m_debugRayLineEntity);
  lineMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  lineMr.shader = basicShader;
  lineMr.isTransparent = true;
  lineMr.isVisible = false;

  m_debugRayHitMarkerEntity = m_debugRayOwner.Create(ctx.world);
  auto &hitT = ctx.world.Add<Transform>(m_debugRayHitMarkerEntity);
  hitT.scale = {0.18f, 0.18f, 0.18f};
  auto &hitMr = ctx.world.Add<MeshRenderer>(m_debugRayHitMarkerEntity);
  hitMr.mesh = ctx.resource.LoadMesh("builtin/sphere");
  hitMr.shader = basicShader;
  hitMr.color = {0.2f, 1.0f, 0.3f, 0.9f};
  hitMr.isTransparent = true;
  hitMr.isVisible = false;
}

void AimPinController::SetDebugRayVisible(core::GameContext &ctx,
                                          bool visible) {
  using namespace game::components;

  if (!visible) {
    if (auto *lineMr = ctx.world.Get<MeshRenderer>(m_debugRayLineEntity)) {
      lineMr->isVisible = false;
    }
    if (auto *hitMr = ctx.world.Get<MeshRenderer>(m_debugRayHitMarkerEntity)) {
      hitMr->isVisible = false;
    }
  }
  // 有効化時は次のレイキャスト発生まで表示しない（EnsureDebugRayEntitiesで
  // 未生成なら生成されるまで何も表示するものがないため）。
}

void AimPinController::UpdateDebugRayVisualization(
    core::GameContext &ctx, const DirectX::XMFLOAT3 &rayOrigin,
    const DirectX::XMFLOAT3 &rayDirection, bool hit,
    const DirectX::XMFLOAT3 &hitPos, float maxDistance) {
  using namespace DirectX;
  using namespace game::components;

  const XMVECTOR dirVec = XMLoadFloat3(&rayDirection);
  const float dirLenSq = XMVectorGetX(XMVector3LengthSq(dirVec));
  if (dirLenSq < 0.0001f) {
    return; // レイが計算できていない（カメラ不正等）ので前回の表示を維持する
  }

  EnsureDebugRayEntities(ctx);

  const XMVECTOR originVec = XMLoadFloat3(&rayOrigin);
  const float visualLength =
      hit ? XMVectorGetX(XMVector3Length(
                XMVectorSubtract(XMLoadFloat3(&hitPos), originVec)))
          : std::min(maxDistance, kDebugRayVisualMaxLength);
  const XMVECTOR endVec =
      hit ? XMLoadFloat3(&hitPos)
          : XMVectorAdd(originVec, XMVectorScale(dirVec, visualLength));

  if (auto *lineT = ctx.world.Get<Transform>(m_debugRayLineEntity)) {
    const XMVECTOR mid = XMVectorAdd(
        originVec, XMVectorScale(XMVectorSubtract(endVec, originVec), 0.5f));
    XMStoreFloat3(&lineT->position, mid);
    lineT->scale = {0.03f, 0.03f, std::max(visualLength, 0.001f)};

    // TrajectoryPredictorと同じ手法: レイ方向をZ軸としたキューブの
    // 回転行列をそのままクォータニオンへ変換して線分に見せる。
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (std::abs(XMVectorGetY(dirVec)) > 0.99f) {
      up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    const XMVECTOR zAxis = dirVec;
    const XMVECTOR xAxis = XMVector3Normalize(XMVector3Cross(up, zAxis));
    const XMVECTOR yAxis = XMVector3Cross(zAxis, xAxis);
    XMMATRIX rotMat = XMMatrixIdentity();
    rotMat.r[0] = xAxis;
    rotMat.r[1] = yAxis;
    rotMat.r[2] = zAxis;
    XMStoreFloat4(&lineT->rotation, XMQuaternionRotationMatrix(rotMat));
  }
  if (auto *lineMr = ctx.world.Get<MeshRenderer>(m_debugRayLineEntity)) {
    // ヒットなら緑、ヒットしなかった（地形と交わらない等）場合は赤。
    lineMr->color = hit ? XMFLOAT4{0.2f, 1.0f, 0.3f, 0.85f}
                        : XMFLOAT4{1.0f, 0.2f, 0.2f, 0.85f};
    lineMr->isVisible = true;
  }

  if (auto *hitT = ctx.world.Get<Transform>(m_debugRayHitMarkerEntity)) {
    if (hit) {
      hitT->position = hitPos;
    }
  }
  if (auto *hitMr = ctx.world.Get<MeshRenderer>(m_debugRayHitMarkerEntity)) {
    hitMr->isVisible = hit;
  }
}

#endif // _DEBUG

} // namespace game::controllers
