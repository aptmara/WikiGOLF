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

/** @brief テクスチャ上のリンクから作るワールド配置候補です。 */
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
 * @brief リンク座標変換とミニマップ用候補選抜を担当します。
 * @details 同じ記事上の近接リンクを間引きつつ、目的地リンクを必ず残し、
 *          最後に記事内の元順序へ戻します。
 */
class HolePlacementPlanner {
public:
  /** @brief リンク矩形の中心をフィールド上の候補へ変換します。 */
  HolePlacementCandidate BuildCandidate(
      const graphics::LinkRegion &link, std::size_t originalIndex,
      std::uint32_t textureWidth, std::uint32_t textureHeight,
      float fieldWidth, float fieldDepth) const;

  /** @brief ミニマップへ表示する候補を間隔と上限に従って選抜します。 */
  std::vector<HolePlacementCandidate> SelectMapCandidates(
      const std::vector<HolePlacementCandidate> &candidates) const;

private:
  /** @brief 選抜済み候補すべてから指定距離以上離れているか判定します。 */
  bool IsFarEnoughFromSelected(
      const std::vector<HolePlacementCandidate> &selected,
      const HolePlacementCandidate &candidate, float minDistance) const;
};

} // namespace game::scenes
