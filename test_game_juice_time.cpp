#include "src/game/systems/GameJuiceSystem.h"
#include "src/game/systems/GameJuiceSurfaceRules.h"
#include "src/resources/ResourceManager.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
  } while (false)

#define CHECK_CLOSE(actual, expected, epsilon, message)                       \
  CHECK(std::fabs((actual) - (expected)) <= (epsilon), message)

namespace resources {

core::ResourceHandle<graphics::Mesh>
ResourceManager::LoadMesh(const std::string &) {
  return {};
}

core::ResourceHandle<graphics::Shader>
ResourceManager::LoadShader(const std::string &, const std::wstring &,
                             const std::wstring &) {
  return {};
}

} // namespace resources

int main() {
  using namespace DirectX;
  using namespace game::systems::juice_detail;
  game::systems::TerrainData slope;
  slope.config.resolutionX = 3;
  slope.config.resolutionZ = 3;
  slope.config.worldWidth = 4.0f;
  slope.config.worldDepth = 4.0f;
  for (int z = 0; z < 3; ++z) {
    for (int x = 0; x < 3; ++x) {
      slope.heightMap.push_back(0.4f * (x * 2.0f - 2.0f) -
                                0.2f * (2.0f - z * 2.0f));
    }
  }
  for (const XMFLOAT3 point : {XMFLOAT3{0.3f, 9.0f, -0.7f},
                              XMFLOAT3{1.98f, 9.0f, 1.98f}}) {
    const auto surface = SampleEffectSurface(&slope, point);
    CHECK_CLOSE(surface.position.y, 0.4f * point.x - 0.2f * point.z + 0.02f,
                0.0001f, "地面の跡は移動先の地形高と描画面オフセットへ追従します");
    const auto expected = XMVector3Normalize(XMVectorSet(-0.4f, 1.0f, 0.2f, 0));
    for (float yaw : {0.0f, 1.2f, 3.0f}) {
      const auto rotation = SurfaceRotation(surface.normal, yaw);
      const auto up = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), XMLoadFloat4(&rotation));
      CHECK_CLOSE(XMVectorGetX(XMVector3Dot(up, expected)), 1.0f, 0.0001f,
                  "輪の向きは接平面内で回しても坂の法線を保ちます");
    }
  }
  const auto absent = SampleEffectSurface(nullptr, {1, 2, 3});
  CHECK_CLOSE(absent.position.y, 2.0f, 0.0001f, "地形のない演出は指定位置を維持します");
  game::systems::GameJuiceSystem system;

  CHECK_CLOSE(system.ConsumeTimeScale(0.0f), 1.0f, 0.0001f,
              "初期状態の時間倍率は1です");

  system.TriggerHitStop(0.2f, 0.0f);
  CHECK_CLOSE(system.ConsumeTimeScale(0.05f), 0.0f, 0.0001f,
              "ヒットストップ中は指定倍率を維持します");
  CHECK_CLOSE(system.ConsumeTimeScale(0.2f), 0.0f, 0.0001f,
              "ヒットストップ終了フレームも停止倍率を維持します");
  CHECK_CLOSE(system.ConsumeTimeScale(0.0f), 1.0f, 0.0001f,
              "ヒットストップ終了後は通常倍率へ戻ります");

  system.TriggerSlowMotion(1.0f, 0.5f);
  const float initialSlowScale = system.ConsumeTimeScale(0.0f);
  CHECK_CLOSE(initialSlowScale, 1.0f, 0.0001f,
              "スローモーション開始時は通常倍率から減速します");
  const float finalSlowScale = system.ConsumeTimeScale(1.0f);
  CHECK_CLOSE(finalSlowScale, 0.5f, 0.0001f,
              "スローモーション経過後は指定倍率になります");

  system.SetTargetFov(10.0f);
  system.TriggerSlowMotion(0.0f, 1.0f);
  CHECK_CLOSE(system.GetCurrentFov(), 60.0f, 0.0001f,
              "FOV設定前の現在値は基準値です");

  std::cout << "[PASS] GameJuiceの時間制御を確認しました\n";
  return 0;
}
