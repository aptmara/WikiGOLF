#pragma once
/**
 * @file CameraSystem.h
 * @brief CameraSystem クラスおよび関連システム
*/

#include "../../core/GameContext.h"

namespace game::systems {

/**
 * @brief 入力に基づいてカメラのTransformを更新するシステム
 * @param ctx ゲームコンテキスト（Input, Worldへのアクセス）
*/
void CameraSystem(core::GameContext& ctx);

} // namespace game::systems
