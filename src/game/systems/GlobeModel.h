#pragma once
/**
 * @file GlobeModel.h
 * @brief タイトル・遷移・リザルト・空に出す地球儀モデルの定義。
 */

#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"

namespace game::systems {

/**
 * @brief 段階的 LOD のモデル（テクスチャも各 GLB に埋め込み）。
 * @details 極軸は Y、半径は 0.5（スケール 1 のとき）。
 */
inline constexpr const char *kGlobeLodModels[] = {
    "Assets/models/golf_puzzle_baked_lod0.glb", // 約 1.6 万三角形 / 4K テクスチャ
    "Assets/models/golf_puzzle_baked_lod1.glb", // 約 4 千三角形 / 2K テクスチャ
    "Assets/models/golf_puzzle_baked_lod2.glb", // 約 1 千三角形 / 1K テクスチャ
};
inline constexpr int kGlobeLodCount = 3;

/** @brief MeshRenderer に、指定 LOD の地球儀のメッシュとテクスチャを設定します。 */
inline void ApplyGlobeModel(components::MeshRenderer &renderer,
                            resources::ResourceManager &resource, int lod = 0) {
  const char *path = kGlobeLodModels[lod];
  const auto textures = resource.LoadModelTextures(path);
  renderer.mesh = resource.LoadMesh(path);
  renderer.textureSRV = textures.baseColor;
  renderer.hasTexture = static_cast<bool>(textures.baseColor);
  renderer.normalMapSRV = textures.normal;
  renderer.hasNormalMap = static_cast<bool>(textures.normal);
}

} // namespace game::systems
