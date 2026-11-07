#include "game/utils/OcclusionFadeRules.h"
#include <cassert>

using DirectX::XMFLOAT3;
using DirectX::XMFLOAT4;
using game::utils::CalculateOcclusionFadeAlpha;
using game::utils::kOcclusionFadeMinAlpha;

int main() {
  const XMFLOAT3 camera{0.0f, 2.0f, 0.0f};
  const XMFLOAT3 ball{0.0f, 0.0f, 10.0f};
  const XMFLOAT4 identity{0.0f, 0.0f, 0.0f, 1.0f};
  const XMFLOAT3 unitScale{1.0f, 1.0f, 1.0f};

  // 線分のちょうど上にある物体は最も薄くなる
  const float blocking = CalculateOcclusionFadeAlpha(
      camera, ball, {0.0f, 1.0f, 5.0f}, identity, unitScale);
  assert(blocking == kOcclusionFadeMinAlpha);

  // 横に大きく離れた物体はそのまま
  const float aside = CalculateOcclusionFadeAlpha(
      camera, ball, {5.0f, 1.0f, 5.0f}, identity, unitScale);
  assert(aside == 1.0f);

  // ボールより奥にある物体は遮っていないのでそのまま
  const float beyond = CalculateOcclusionFadeAlpha(
      camera, ball, {0.0f, 0.0f, 14.0f}, identity, unitScale);
  assert(beyond == 1.0f);

  // カメラより後ろの物体もそのまま
  const float behind = CalculateOcclusionFadeAlpha(
      camera, ball, {0.0f, 2.0f, -4.0f}, identity, unitScale);
  assert(behind == 1.0f);

  // 細い幹: 横に 0.6 ずれると単位球なら遮るが、幅 0.28 の幹なら遮らない
  const float thinTrunk = CalculateOcclusionFadeAlpha(
      camera, ball, {0.6f, 1.0f, 5.0f}, identity, {0.28f, 2.0f, 0.28f});
  assert(thinTrunk == 1.0f);
  const float wideSphere = CalculateOcclusionFadeAlpha(
      camera, ball, {0.6f, 1.0f, 5.0f}, identity, {2.0f, 2.0f, 2.0f});
  assert(wideSphere < 0.5f);

  // 縁の近くは途中の値になる
  const float edge = CalculateOcclusionFadeAlpha(
      camera, ball, {0.7f, 1.0f, 5.0f}, identity, unitScale);
  assert(edge > kOcclusionFadeMinAlpha && edge < 1.0f);

  return 0;
}
