#pragma once

/**
 * @file WikiGolfSceneSupport.h
 * @brief WikiGolfシーンで共有する定数と初期化処理を定義します。
 */

namespace core {
struct GameContext;
}

namespace game::scenes::scene_detail {

inline constexpr float kFieldScale = 4.0f;
inline constexpr float kMinMapViewSpan = 5.0f;

/**
 * @brief ゲーム中に使う描画リソースを事前に読み込みます。
 */
void PreloadGameplayResources(core::GameContext& ctx);

} // namespace game::scenes::scene_detail
