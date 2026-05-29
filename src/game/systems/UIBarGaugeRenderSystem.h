#pragma once
/**
 * @file UIBarGaugeRenderSystem.h
 * @brief UIBarGaugeコンポーネントを描画するシステム
 */

#include "../../core/GameContext.h"

namespace game::systems {

class UIBarGaugeRenderSystem {
public:
  void operator()(core::GameContext &ctx);
};

} // namespace game::systems
