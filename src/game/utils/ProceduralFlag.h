#pragma once
/**
 * @file ProceduralFlag.h
 * @brief プロシージャル旗生成ユーティリティ
 */

#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::utils {

/**
 * @brief プロシージャル旗の生成設定です。
 */
struct ProceduralFlagOptions {
  uint32_t holeEntity = UINT32_MAX; ///< 紐づけるホール。説明用は UINT32_MAX。
  bool createParticles = false;     ///< 旗周辺の低密度粒子を生成するか。
  bool large = false;               ///< 目的地用にやや大きくするか。
  float animationWeight = 0.7f;      ///< なびきと粒子の強さ。
};

/**
 * @brief 生成した旗関連エンティティです。
 */
struct ProceduralFlagResult {
  ecs::Entity poleEntity = UINT32_MAX;
  ecs::Entity firstClothEntity = UINT32_MAX;
  std::vector<ecs::Entity> allEntities;
  std::vector<ecs::Entity> particleEntities;
};

/**
 * @brief ポール、キャップ、分割旗布、任意の粒子を生成します。
 * @param ctx ゲームコンテキスト
 * @param basePosition 旗の根元位置
 * @param color 旗色
 * @param options 生成設定
 * @return 生成したエンティティ群
 */
ProceduralFlagResult CreateProceduralFlag(
    core::GameContext &ctx, const DirectX::XMFLOAT3 &basePosition,
    const DirectX::XMFLOAT4 &color, const ProceduralFlagOptions &options);

} // namespace game::utils
