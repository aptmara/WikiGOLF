#pragma once
/**
 * @file ShotGuideController.h
 * @brief ショット予測表示の窓口
 *
 * シーンは「どこから・どの向きへ・どの強さで打つか」を ShotGuideInput に詰めて
 * 渡すだけ。ここで道すじを計算し（条件が変わったときだけ）、動きを進め、
 * 登録してある描画部品へ1フレームぶんの表示内容を配る。
 *
 * ミニマップなど外から道すじを使いたい側は GetVisiblePath() の結果だけを読む。
*/

#include "../../utils/ShotGuideAnimation.h"
#include "../../utils/ShotGuidePath.h"
#include "../../utils/ShotGuideStyle.h"
#include "../../utils/ShotGuideZone.h"
#include "ShotGuideView.h"
#include <memory>
#include <vector>

namespace game::systems {
class WikiTerrainSystem;
}

namespace game::guide {

/** @brief シーンから渡す条件*/
struct ShotGuideInput {
  bool visible = false;      ///< 表示するか
  ShotGuideRequest request;  ///< 打ち出しの条件
  game::systems::WikiTerrainSystem *terrain = nullptr;
  float energy = 0.0f;       ///< 表示するパワー比率(0〜1)
  bool charging = false;     ///< ゲージ操作中か（色と動きが熱を帯びる）
  ShotGuideSpread spread;    ///< ショットのぶれの大きさ（止まりそうな範囲の広さ）
  /** @brief 壁や障害物。nullptr なら地形だけを相手にする*/
  const IShotGuideObstacles *obstacles = nullptr;
  /** @brief 壁や障害物の中身が変わったら変える番号*/
  int obstacleRevision = 0;
  /** @brief クラブなど「これが変わったら描き直す」ものを表す値*/
  int contextKey = 0;
};

class ShotGuideController {
public:
  ShotGuideController();
  ~ShotGuideController();

  void Initialize(core::GameContext &ctx);
  void Update(core::GameContext &ctx, const ShotGuideInput &input);
  void Hide(core::GameContext &ctx);
  void Shutdown(core::GameContext &ctx);

  /** @brief いま表示している道すじ。表示していなければ nullptr*/
  const ShotGuidePath *GetVisiblePath() const {
    return m_visible && m_path.valid ? &m_path : nullptr;
  }

  /** @brief いま表示している「止まりそうな範囲」。表示していなければ nullptr*/
  const ShotGuideZone *GetVisibleZone() const {
    return m_visible && m_path.valid && m_zone.valid ? &m_zone : nullptr;
  }

  /** @brief 表示中か*/
  bool IsVisible() const { return m_visible; }

  /** @brief 見た目の調整値（デバッグ時の調整用）*/
  ShotGuideStyle &Style() { return m_style; }

private:
  std::vector<std::unique_ptr<IShotGuideView>> m_views;
  ShotGuideStyle m_style;
  ShotGuideAnimation m_animation;

  ShotGuidePath m_path;
  ShotGuideZone m_zone;
  const IShotGuideObstacles *m_cachedObstacles = nullptr;
  int m_cachedObstacleRevision = 0;
  ShotGuideRequest m_cachedRequest;
  game::systems::WikiTerrainSystem *m_cachedTerrain = nullptr;
  bool m_hasCache = false;
  int m_contextKey = 0;
  bool m_visible = false;
  bool m_initialized = false;
};

} // namespace game::guide
