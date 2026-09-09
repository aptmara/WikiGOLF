#pragma once
/**
 * @file BallFastForwardIndicator.h
 * @brief 倍速演出中（1.5x/2.0x）に表示するインジケーターUIの表示制御
 *
 * 入力: BallFastForwardTimerが決定した現在の倍速段階
 * 出力: 表示先エンティティのUIImageコンポーネント（テクスチャ/透明度/位置）
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../utils/BallFastForwardState.h"

namespace game::controllers {

/**
 * @brief 「1.5倍速」「2.0倍速」を示すインジケーター画像の
 *        表示・フェード・テクスチャ切り替えを担当します。
 * @details 表示先のEntity自体はシーン側（WikiGolfScene）が生成・破棄の責任を
 *          持ち、このクラスは渡されたEntityが持つUIImageコンポーネントの
 *          値を更新するだけに留めます。
*/
class BallFastForwardIndicator {
public:
  /**
   * @brief 表示先エンティティを登録し、非表示状態に初期化します。
   * @param ctx ゲームコンテキスト
   * @param entity UIImageコンポーネントを持つ表示先エンティティ
  */
  void Initialize(core::GameContext &ctx, ecs::Entity entity);

  /** @brief 内部状態をリセットします（エンティティ自体の破棄はシーン側の責任）。*/
  void Shutdown();

  /**
   * @brief 現在の倍速段階に応じてインジケーターの見た目を更新します。
   * @param ctx ゲームコンテキスト
   * @param dt 実時間のデルタタイム（演出アニメーション用、速度倍率の影響を受けない値）
   * @param tier 現在の倍速段階（BallFastForwardTimer::GetCurrentTier()）
  */
  void Update(core::GameContext &ctx, float dt, game::utils::FastForwardTier tier);

private:
  ecs::Entity m_entity = UINT32_MAX; ///< 表示先エンティティ
  game::utils::FastForwardTier m_displayedTier =
      game::utils::FastForwardTier::Normal; ///< 直近に表示していた段階（切り替え検知用）
  float m_visibleSeconds = 0.0f; ///< 現在の段階を表示し始めてからの経過秒数
  float m_currentAlpha = 0.0f;   ///< フェード用の現在の透明度
};

} // namespace game::controllers
