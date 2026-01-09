#pragma once
/**
 * @file WikiGameSystem.h
 * @brief WikiPinball固有のゲームロジック（衝突判定後の処理など）
 */

#include "../../core/GameContext.h"
#include <string>

namespace game::systems {

/**
 * @brief ゲームルールの更新処理
 *
 * 物理演算システムが生成した衝突イベントを処理し、
 * スコア計算、オブジェクト破壊、UI更新などを行います。
 */
void WikiGameSystem(core::GameContext &ctx);

} // namespace game::systems
