#include "src/game/systems/GameJuiceSystem.h"
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
