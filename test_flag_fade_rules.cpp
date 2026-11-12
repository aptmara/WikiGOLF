#include "game/utils/FlagFadeRules.h"
#include "game/utils/ProceduralFlag.h"
#include <cassert>

using DirectX::XMFLOAT3;
using DirectX::XMFLOAT4;
using game::utils::CalculateBillboardFadeAlpha;
using game::utils::CalculateFlagFadeAlpha;

int main() {
  static_assert(game::utils::GetProceduralFlagPoleHeight(true) ==
                game::utils::kProceduralFlagPoleHeightScale *
                    game::utils::kProceduralFlagLargeSize);
  static_assert(game::utils::GetProceduralFlagPoleHeight(false) ==
                game::utils::kProceduralFlagPoleHeightScale *
                    game::utils::kProceduralFlagNormalSize);
  static_assert(game::utils::GetProceduralFlagPoleHeight(true) >
                game::utils::GetProceduralFlagPoleHeight(false));

  const XMFLOAT3 camera{0.0f, 1.5f, 0.0f};

  const float nearAlpha = CalculateFlagFadeAlpha(
      camera, {0.0f, 1.5f, 0.5f}, {4.0f, 1.5f, 8.0f});
  assert(nearAlpha == 0.0f);

  const float clearAlpha = CalculateFlagFadeAlpha(
      camera, {4.0f, 1.5f, 4.0f}, {0.0f, 1.5f, 8.0f});
  assert(clearAlpha == 1.0f);

  const float occludingAlpha = CalculateFlagFadeAlpha(
      camera, {0.0f, 1.5f, 4.0f}, {0.0f, 1.5f, 8.0f});
  assert(occludingAlpha == 0.0f);

  const float behindGolferAlpha = CalculateFlagFadeAlpha(
      camera, {0.0f, 1.5f, 10.0f}, {0.0f, 1.5f, 8.0f});
  assert(behindGolferAlpha == 1.0f);

  // ロボットは遮らないが、ボールを遮る旗は薄くなる
  const float blocksBallAlpha = CalculateFlagFadeAlpha(
      camera, {6.0f, 1.5f, 6.0f}, {0.0f, 1.5f, 8.0f}, {8.0f, 1.5f, 8.0f});
  assert(blocksBallAlpha == 0.0f);

  // どちらも遮らない旗はそのまま
  const float clearBothAlpha = CalculateFlagFadeAlpha(
      camera, {-6.0f, 1.5f, 4.0f}, {0.0f, 1.5f, 8.0f}, {8.0f, 1.5f, 8.0f});
  assert(clearBothAlpha == 1.0f);

  const XMFLOAT4 identityRotation{0.0f, 0.0f, 0.0f, 1.0f};
  const XMFLOAT3 billboardScale{4.0f, 3.0f, 1.0f};

  const float billboardCenterAlpha = CalculateBillboardFadeAlpha(
      camera, {0.0f, 1.5f, 4.0f}, identityRotation, billboardScale,
      {0.0f, 1.5f, 8.0f});
  assert(billboardCenterAlpha == 0.0f);

  // 中心から外れていても、画像の端と重なっていれば薄くなる
  const float billboardEdgeAlpha = CalculateBillboardFadeAlpha(
      camera, {1.9f, 1.5f, 4.0f}, identityRotation, billboardScale,
      {0.0f, 1.5f, 8.0f});
  assert(billboardEdgeAlpha == 0.0f);

  const float billboardClearAlpha = CalculateBillboardFadeAlpha(
      camera, {4.0f, 1.5f, 4.0f}, identityRotation, billboardScale,
      {0.0f, 1.5f, 8.0f});
  assert(billboardClearAlpha == 1.0f);

  const float billboardBehindTargetAlpha = CalculateBillboardFadeAlpha(
      camera, {0.0f, 1.5f, 10.0f}, identityRotation, billboardScale,
      {0.0f, 1.5f, 8.0f});
  assert(billboardBehindTargetAlpha == 1.0f);

  // ロボットは遮らないが、ボールと画像の端が重なる場合も薄くなる
  const float billboardBlocksBallAlpha = CalculateBillboardFadeAlpha(
      camera, {5.9f, 1.5f, 4.0f}, identityRotation, billboardScale,
      {0.0f, 1.5f, 8.0f}, {8.0f, 1.5f, 8.0f});
  assert(billboardBlocksBallAlpha == 0.0f);

  return 0;
}
