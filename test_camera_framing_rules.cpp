#include "game/utils/CameraFramingRules.h"
#include <DirectXMath.h>
#include <cassert>
#include <cmath>

namespace {

constexpr float kTolerance = 0.0001f;
constexpr float kScreenTolerance = 0.01f;
constexpr float kFov = DirectX::XMConvertToRadians(60.0f);
constexpr float kDistance = 5.0f;

bool Close(float left, float right, float tolerance = kTolerance) {
  return std::abs(left - right) <= tolerance;
}

float ScreenY(const DirectX::XMFLOAT3 &target, float pitch) {
  const float horizontal = std::sqrt(target.x * target.x + target.z * target.z);
  return game::utils::TargetScreenYForOrbitPitch(pitch, kDistance, horizontal,
                                                 target.y, kFov);
}

} // namespace

int main() {
  using game::utils::FindTargetCenteredOrbitAngles;
  using game::utils::InitialOrbitDistance;
  using game::utils::kFramingTargetScreenY;

  const DirectX::XMFLOAT3 ball{0.0f, 0.0f, 0.0f};

  assert(Close(InitialOrbitDistance(4.0f), 42.0f));

  {
    // 平地の遠い目標でも水平にはならず、見下ろしつつ画面上部に収める
    const DirectX::XMFLOAT3 visibleTarget{0.0f, 0.0f, 80.0f};
    const auto result = FindTargetCenteredOrbitAngles(
        ball, visibleTarget, 0.0f, 0.35f, kDistance, kFov);
    assert(Close(result.yaw, 0.0f));
    assert(result.pitch > 0.2f);
    assert(Close(ScreenY(visibleTarget, result.pitch), kFramingTargetScreenY,
                 kScreenTolerance));
  }

  {
    const DirectX::XMFLOAT3 rightTarget{100.0f, 0.0f, 0.0f};
    const auto result = FindTargetCenteredOrbitAngles(
        ball, rightTarget, 0.0f, 0.35f, kDistance, kFov);
    assert(Close(result.yaw, DirectX::XM_PIDIV2));
    assert(result.pitch > 0.2f);
  }

  {
    // 近い目標ほど深く見下ろす
    const DirectX::XMFLOAT3 nearTarget{0.0f, 0.0f, 6.0f};
    const DirectX::XMFLOAT3 farTarget{0.0f, 0.0f, 120.0f};
    const auto nearResult = FindTargetCenteredOrbitAngles(
        ball, nearTarget, 0.0f, 0.5f, kDistance, kFov);
    const auto farResult = FindTargetCenteredOrbitAngles(
        ball, farTarget, 0.0f, 0.5f, kDistance, kFov);
    assert(nearResult.pitch > farResult.pitch);
    assert(Close(ScreenY(nearTarget, nearResult.pitch), kFramingTargetScreenY,
                 kScreenTolerance));
  }

  {
    // 高い目標は最小ピッチでも画面外になりやすいので最小ピッチに留める
    const DirectX::XMFLOAT3 highTarget{0.0f, 60.0f, 20.0f};
    const auto result = FindTargetCenteredOrbitAngles(
        ball, highTarget, 0.0f, 1.0f, kDistance, kFov);
    assert(Close(result.pitch, game::utils::kFramingMinPitch));
  }

  return 0;
}
