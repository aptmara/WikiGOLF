#include "game/utils/CameraFramingRules.h"
#include <DirectXMath.h>
#include <cassert>
#include <cmath>

namespace {

constexpr float kTolerance = 0.0001f;

bool Close(float left, float right) {
  return std::abs(left - right) <= kTolerance;
}

} // namespace

int main() {
  using game::utils::FindTargetCenteredOrbitAngles;

  const DirectX::XMFLOAT3 ball{0.0f, 0.0f, 0.0f};

  {
    const DirectX::XMFLOAT3 visibleTarget{0.0f, 0.0f, 80.0f};
    const float yaw = 0.0f;
    const float pitch = 0.35f;
    const auto result =
        FindTargetCenteredOrbitAngles(ball, visibleTarget, yaw, pitch);
    assert(Close(result.yaw, yaw));
    assert(Close(result.pitch, 0.0f));
  }

  {
    const DirectX::XMFLOAT3 rightTarget{100.0f, 0.0f, 0.0f};
    const auto result =
        FindTargetCenteredOrbitAngles(ball, rightTarget, 0.0f, 0.35f);
    assert(Close(result.yaw, DirectX::XM_PIDIV2));
    assert(Close(result.pitch, 0.0f));
  }

  {
    const DirectX::XMFLOAT3 forwardTarget{0.0f, 24.0f, 120.0f};
    const auto result =
        FindTargetCenteredOrbitAngles(ball, forwardTarget, 0.0f, 1.0f);
    assert(Close(result.yaw, 0.0f));
    assert(Close(result.pitch, -std::atan2(24.0f, 120.0f)));
  }

  return 0;
}
