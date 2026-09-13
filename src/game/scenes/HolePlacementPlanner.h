#pragma once
/**
 * @file HolePlacementPlanner.h
 * @brief Wikipediaリンク領域からホール配置候補を計画するクラス
*/

#include "../../graphics/WikiTextureGenerator.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace game::scenes {

/** @brief テクスチャ上のリンクから作るワールド配置候補です。*/
struct HolePlacementCandidate {
  float x = 0.0f;
  float z = 0.0f;
  std::string linkTarget;
  bool isTarget = false;
  int hopsToTarget = -1;
  std::size_t originalIndex = 0;
  bool isPlayable = false;
};

/**
 * @brief リンク座標変換とミニマップ用候補作成を担当します。
*/
class HolePlacementPlanner {
public:
  /** @brief リンク矩形の中心をフィールド上の候補へ変換します。*/
  HolePlacementCandidate BuildCandidate(
      const graphics::LinkRegion &link, std::size_t originalIndex,
      std::uint32_t textureWidth, std::uint32_t textureHeight,
      float fieldWidth, float fieldDepth) const;

  /** @brief 全候補を元の順序のままミニマップ表示へ渡します。*/
  std::vector<HolePlacementCandidate> SelectMapCandidates(
      const std::vector<HolePlacementCandidate> &candidates) const;
};

} // namespace game::scenes
