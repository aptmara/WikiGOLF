#pragma once
/**
 * @file PhysicsSystem.h
 * @brief 物理演算システム
 */

#include "../../core/GameContext.h"

namespace game::systems {

/**
 * @brief 物理演算の更新処理を行います
 *
 * オイラー法による移動積分と、簡易的な球体・矩形の衝突解決を行います。
 *
 * @param ctx ゲームコンテキスト (World, Input等へのアクセス)
 * @param dt デルタタイム (秒)
 */
void PhysicsSystem(core::GameContext &ctx, float dt);

} // namespace game::systems
