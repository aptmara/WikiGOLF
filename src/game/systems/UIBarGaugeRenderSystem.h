#pragma once
/**
 * @file UIBarGaugeRenderSystem.h
 * @brief UIBarGaugeコンポーネントを描画するシステム
*/

#include "../../core/GameContext.h"

namespace game::systems {

/** @brief UIBarGaugeコンポーネントを描画するシステム */
class UIBarGaugeRenderSystem {
public:
  /**
   * @brief バーゲージ描画を実行します。
   * @param ctx ゲームコンテキスト
   */
  void operator()(core::GameContext &ctx);
};

} // namespace game::systems
