#include "src/game/components/WikiComponents.h"
#include "src/game/utils/BallStopRules.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
  } while (false)

#define CHECK_CLOSE(actual, expected, epsilon, message)                       \
  CHECK(std::fabs((actual) - (expected)) <= (epsilon), message)

int main() {
  using game::utils::FreezeBall;
  using game::utils::UpdateBallStopConfirmation;

  float candidateSeconds = 0.0f;
  CHECK(!UpdateBallStopConfirmation(0.0f, true, 1.0f / 60.0f,
                                    candidateSeconds),
        "一瞬の速度0では停止を確定しません");
  CHECK(candidateSeconds > 0.0f,
        "低速かつ接地している時間を蓄積します");

  CHECK(!UpdateBallStopConfirmation(0.0f, true, 0.30f, candidateSeconds),
        "合計0.5秒未満では停止を確定しません");
  CHECK(!UpdateBallStopConfirmation(0.2f, true, 0.01f, candidateSeconds),
        "速度がしきい値へ戻ると停止候補を解除します");
  CHECK_CLOSE(candidateSeconds, 0.0f, 0.0001f,
              "停止候補時間を0へ戻します");

  CHECK(!UpdateBallStopConfirmation(0.0f, true, 0.49f, candidateSeconds),
        "連続0.49秒では停止を確定しません");
  CHECK(UpdateBallStopConfirmation(0.0f, true, 0.02f, candidateSeconds),
        "連続0.5秒以上で停止を確定します");

  CHECK(!UpdateBallStopConfirmation(0.0f, false, 0.1f, candidateSeconds),
        "非接地になると停止候補を解除します");
  CHECK_CLOSE(candidateSeconds, 0.0f, 0.0001f,
              "非接地時も停止候補時間を0へ戻します");

  game::components::RigidBody body;
  body.velocity = {1.0f, 2.0f, 3.0f};
  body.acceleration = {4.0f, 5.0f, 6.0f};
  body.angularVelocity = {7.0f, 8.0f, 9.0f};
  FreezeBall(body);

  CHECK(body.isStatic, "停止確定後は物理演算対象から外します");
  CHECK_CLOSE(std::fabs(body.velocity.x) + std::fabs(body.velocity.y) +
                  std::fabs(body.velocity.z),
              0.0f, 0.0001f, "停止確定時に速度を消去します");
  CHECK_CLOSE(std::fabs(body.acceleration.x) + std::fabs(body.acceleration.y) +
                  std::fabs(body.acceleration.z),
              0.0f, 0.0001f, "停止確定時に加速度を消去します");
  CHECK_CLOSE(std::fabs(body.angularVelocity.x) +
                  std::fabs(body.angularVelocity.y) +
                  std::fabs(body.angularVelocity.z),
              0.0f, 0.0001f, "停止確定時に角速度を消去します");

  game::components::ShotState shot;
  shot.stopCandidateTime = 0.4f;
  shot.Reset();
  CHECK_CLOSE(shot.stopCandidateTime, 0.0f, 0.0001f,
              "ショット状態のリセット時に停止候補時間も消去します");

  std::cout << "[PASS] ボール停止判定ルールを確認しました\n";
  return 0;
}
