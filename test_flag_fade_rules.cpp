#include "game/utils/FlagFadeRules.h"
#include "game/utils/ProceduralFlag.h"
#include <cassert>

using DirectX::XMFLOAT3;
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

  return 0;
}
