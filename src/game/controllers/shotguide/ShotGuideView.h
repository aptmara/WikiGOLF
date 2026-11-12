#pragma once
/**
 * @file ShotGuideView.h
 * @brief ショット予測表示の描画部品が共通で受け取るデータと、部品の約束
 *
 * 部品（予測線・着地点の印・光の玉・足元の向き表示）は ShotGuideFrame だけを
 * 読んで自分の Entity を動かす。クラブ・ゲージ・シーンのことは知らない。
 * 部品どうしも互いを知らないので、足すのも外すのも ShotGuideController の
 * 1行で済む。
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include "../../utils/ShotGuidePath.h"
#include "../../utils/ShotGuideStyle.h"
#include "../../utils/ShotGuideZone.h"
#include <DirectXMath.h>

namespace core {
struct GameContext;
}

namespace game::guide {

/** @brief 1フレームぶんの表示内容*/
struct ShotGuideFrame {
  const ShotGuidePath *path = nullptr;   ///< 描く道すじ（Update 中は必ず有効）
  const ShotGuideZone *zone = nullptr;   ///< 止まりそうな範囲（Update 中は必ず有効）
  const ShotGuideStyle *style = nullptr; ///< 見た目の調整値
  GuidePalette palette;                  ///< 今のパワーに応じた色
  DirectX::XMFLOAT3 aimDirection{0.0f, 0.0f, 1.0f}; ///< 水平の狙い方向

  float time = 0.0f;      ///< 表示を始めてからの時間[秒]
  float fade = 1.0f;      ///< 全体の濃さ(0〜1)
  float energy = 0.0f;    ///< 表示中のパワー比率(0〜1)
  float flowPhase = 0.0f; ///< 線の光が流れた道のり[m]
  float aimPhase = 0.0f;  ///< 足元の矢羽根の位相(0〜1)
  float spin = 0.0f;      ///< 輪の回転[rad]
  float reveal = 1.0f;    ///< 線の描き始め(0〜1)
  float pop = 1.0f;       ///< 印の出現(0〜1)
  float ghostArc = 0.0f;  ///< 光の玉がいる道のり[m]
  float ghostAlpha = 0.0f;     ///< 光の玉の濃さ
  float landingRipple = -1.0f; ///< 着地の波紋(0〜1)。負なら無し
  float restRipple = -1.0f;    ///< 静止の波紋(0〜1)。負なら無し
  float scale = 1.0f; ///< 表示の大きさ。近いショットほど小さく控えめにする
};

/** @brief 描画部品の約束*/
class IShotGuideView {
public:
  virtual ~IShotGuideView() = default;
  virtual void Initialize(core::GameContext &ctx) = 0;
  virtual void Update(core::GameContext &ctx, const ShotGuideFrame &frame) = 0;
  virtual void Hide(core::GameContext &ctx) = 0;
  virtual void Shutdown(core::GameContext &ctx) = 0;
};

/**
 * @brief ShotGuide シェーダーで描く板を1枚作る（最初は非表示）。
 * @param shapeId game::guide::shape の番号
 * @param inFront 地形や芝に隠れても見えるよう、他より手前に描くか
*/
ecs::Entity CreateGuideQuad(core::GameContext &ctx, ecs::EntityOwner &owner,
                            float shapeId, bool inFront = false);

/** @brief 板を隠す*/
void HideGuideQuad(core::GameContext &ctx, ecs::Entity entity);

/**
 * @brief 板を地面に寝かせて置く。
 * @param normal 地面の向き
 * @param forward 板の「前」を向ける水平方向
 * @param spin 地面の向きを軸にした回転[rad]
 * @param size 板の一辺[m]
 * @param shapeValue 形ごとの値（波紋の進み具合・模様の位相など）
 * @param sizeForward 「前」方向の一辺[m]。0以下なら size と同じ（正方形）
*/
void PlaceGroundQuad(core::GameContext &ctx, ecs::Entity entity,
                     const DirectX::XMFLOAT3 &position,
                     const DirectX::XMFLOAT3 &normal,
                     const DirectX::XMFLOAT3 &forward, float spin, float size,
                     const GuideColor &color, float shapeValue = 0.0f,
                     float sizeForward = 0.0f);

/**
 * @brief 板を from→to に沿った帯として置く（幅方向はシェーダーがカメラへ向ける）。
 * @return 置けたら true。短すぎて置けないときは隠して false。
*/
bool PlaceRibbonQuad(core::GameContext &ctx, ecs::Entity entity,
                     const DirectX::XMFLOAT3 &from,
                     const DirectX::XMFLOAT3 &to, float width,
                     const GuideColor &color, float shapeId, float shapeValue);

} // namespace game::guide
