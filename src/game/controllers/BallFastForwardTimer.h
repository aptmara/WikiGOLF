#pragma once
/**
 * @file BallFastForwardTimer.h
 * @brief ショット実行中、ボール着地後の経過時間から物理シミュレーションの
 *        速度倍率（倍速段階）を決定するタイマー
 *
 * 入力: GolfGameState::isBallGrounded, ShotState::phase
 * 出力: 物理dtへ掛け合わせる速度倍率、現在の倍速段階
*/

#include "../../core/GameContext.h"
#include "../utils/BallFastForwardState.h"

namespace game::controllers {

/**
 * @brief 着地後の経過時間を計測し、倍速段階（Normal/1.5x/2.0x）を管理するタイマー。
 * @details ショットが実行中（ShotState::Phase::Executing）でなくなった時点で
 *          自動的にリセットされ、次のショットの着地を新たに待ち受けます。
*/
class BallFastForwardTimer {
public:
  /** @brief 未着地・通常速度の初期状態へ戻します。*/
  void Reset();

  /**
   * @brief 1フレーム分の状態を更新し、物理dtに掛け合わせる速度倍率を返します。
   * @param ctx ゲームコンテキスト（GolfGameState/ShotStateの参照に使用）
   * @param dt 実時間のデルタタイム（速度倍率適用前の秒数）
   * @return 物理シミュレーションdtに掛け合わせる速度倍率（1.0 / 1.5 / 2.0）
  */
  float Update(core::GameContext &ctx, float dt);

  /** @brief 現在の倍速段階を返します。*/
  game::utils::FastForwardTier GetCurrentTier() const { return m_currentTier; }

private:
  bool m_hasLanded = false;           ///< 今回のショットで一度でも接地したか
  float m_secondsSinceLanding = 0.0f; ///< 着地してからの経過秒数
  game::utils::FastForwardTier m_currentTier =
      game::utils::FastForwardTier::Normal; ///< 現在の倍速段階
};

} // namespace game::controllers
