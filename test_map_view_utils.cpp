#include "src/game/utils/MapViewState.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      return 1;                                                                \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using game::utils::ClampMapCenter;
  using game::utils::ClampMapZoom;

  // 中心が範囲外の場合、フィールド端＋パディング内に収まる
  DirectX::XMFLOAT2 center{200.0f, -300.0f};
  auto clamped = ClampMapCenter(center, 100.0f, 150.0f, 5.0f);
  CHECK(std::abs(clamped.x - 45.0f) < 1e-4f,
        "ClampMapCenter clamps X to width/2 - padding");
  CHECK(std::abs(clamped.y + 70.0f) < 1e-4f,
        "ClampMapCenter clamps Y(Z) to depth/2 - padding");

  // ズームは最小・最大でクランプされる
  CHECK(std::abs(ClampMapZoom(0.1f, 0.3f, 2.0f) - 0.3f) < 1e-4f,
        "ClampMapZoom clamps to min");
  CHECK(std::abs(ClampMapZoom(3.5f, 0.3f, 2.0f) - 2.0f) < 1e-4f,
        "ClampMapZoom clamps to max");
  CHECK(std::abs(ClampMapZoom(1.2f, 0.3f, 2.0f) - 1.2f) < 1e-4f,
        "ClampMapZoom leaves in-range values");

  // フィールドが小さい場合はデフォルト上限をそのまま返す
  CHECK(std::abs(game::utils::CalculateMaxMapZoom(50.0f, 5.0f, 15.0f) - 15.0f) <
            1e-4f,
        "CalculateMaxMapZoom keeps base cap for small maps");

  // 大きいフィールドでも最小ビュー幅5mまで寄れる上限を返す
  float expectedZoom = 500.0f / 5.0f; // extent / minViewSpan
  CHECK(std::abs(game::utils::CalculateMaxMapZoom(500.0f, 5.0f, 15.0f) -
                 expectedZoom) < 1e-4f,
        "CalculateMaxMapZoom scales with field extent for deep zoom");

  std::cout << "All MapView utils tests passed.\n";
  return 0;
}
