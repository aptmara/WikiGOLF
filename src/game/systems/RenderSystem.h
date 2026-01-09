#pragma once
/**
 * @file RenderSystem.h
 * @brief 描画システム（関数定義）
 */

#include "../../core/GameContext.h"

namespace game::systems {

/// @brief 描画システム関数
/// @param ctx ゲームコンテキスト
void RenderSystem(core::GameContext& ctx);

} // namespace game::systems
