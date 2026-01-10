#pragma once
/**
 * @file SkyboxRenderSystem.h
 * @brief スカイボックス描画システム
 */

#include "../../core/GameContext.h"

namespace game::systems {

/**
 * @brief スカイボックスレンダリングシステム
 *
 * @details
 * - RenderSystemの前に呼び出す必要がある
 * - 深度テストはLESS_EQUALで深度書き込みは無効
 * - カリングは反転（内側からキューブを見る）
 */
void SkyboxRenderSystem(core::GameContext &ctx);

} // namespace game::systems
