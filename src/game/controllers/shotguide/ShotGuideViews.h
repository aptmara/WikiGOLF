#pragma once
/**
 * @file ShotGuideViews.h
 * @brief ショット予測表示の描画部品（予測線・止まりそうな範囲・光の玉・足元の向き表示）
 *
 * どれも IShotGuideView の約束だけを守る独立した部品で、互いを参照しない。
*/

#include "ShotGuideView.h"
#include <vector>

namespace game::guide {

/** @brief 予測線。飛行区間は光が流れる帯、転がり区間は流れる点線*/
class TrajectoryRibbonView final : public IShotGuideView {
public:
  void Initialize(core::GameContext &ctx) override;
  void Update(core::GameContext &ctx, const ShotGuideFrame &frame) override;
  void Hide(core::GameContext &ctx) override;
  void Shutdown(core::GameContext &ctx) override;

private:
  std::vector<ecs::Entity> m_segments;     ///< 通常の帯
  std::vector<ecs::Entity> m_xraySegments; ///< 地形や芝に隠れた部分を薄く見せる帯
  ecs::EntityOwner m_owner;
};

/**
 * @brief 「だいたいこのあたりに止まる」範囲の表示。
 * @details 地面に沿って回る点線のふち、淡い内側、光の柱、着地と静止の波紋。
 *          1点を指す印は置かない（実際のショットはぶれるので当たらない）。
*/
class LandingZoneView final : public IShotGuideView {
public:
  void Initialize(core::GameContext &ctx) override;
  void Update(core::GameContext &ctx, const ShotGuideFrame &frame) override;
  void Hide(core::GameContext &ctx) override;
  void Shutdown(core::GameContext &ctx) override;

private:
  std::vector<ecs::Entity> m_rim;     ///< ふちの点線
  std::vector<ecs::Entity> m_rimXray; ///< 地形や芝に隠れたふちを薄く見せる
  ecs::Entity m_fill = UINT32_MAX;
  ecs::Entity m_restRipple = UINT32_MAX;
  ecs::Entity m_landingRipple = UINT32_MAX;
  ecs::Entity m_beam = UINT32_MAX;
  ecs::EntityOwner m_owner;
};

/** @brief 道すじを実際の飛び方の速さで走る光の玉*/
class GhostBallView final : public IShotGuideView {
public:
  void Initialize(core::GameContext &ctx) override;
  void Update(core::GameContext &ctx, const ShotGuideFrame &frame) override;
  void Hide(core::GameContext &ctx) override;
  void Shutdown(core::GameContext &ctx) override;

private:
  ecs::Entity m_orb = UINT32_MAX;
  ecs::EntityOwner m_owner;
};

/** @brief ボールの足元の向き表示（前へ流れる矢羽根と、地面に固定した方位目盛り）*/
class AimIndicatorView final : public IShotGuideView {
public:
  void Initialize(core::GameContext &ctx) override;
  void Update(core::GameContext &ctx, const ShotGuideFrame &frame) override;
  void Hide(core::GameContext &ctx) override;
  void Shutdown(core::GameContext &ctx) override;

private:
  ecs::Entity m_arrow = UINT32_MAX;
  ecs::Entity m_bezel = UINT32_MAX;
  ecs::EntityOwner m_owner;
};

} // namespace game::guide
