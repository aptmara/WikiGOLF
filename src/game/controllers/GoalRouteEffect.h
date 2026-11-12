#pragma once
/**
 * @file GoalRouteEffect.h
 * @brief ラウンド開始時、記事の紙にスタートからゴールまでの道のりを描き、
 *        コースと同じ旗を順に立てる演出
*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include "../utils/GoalNoticeRules.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::controllers {

class ArticlePaperEffect;

/**
 * @brief 道のりの線・ボール・旗・文字を管理します。
 * @details 旗とボールは本物の3D物体を紙に貼り付け、紙と一緒に倒します。
 *          途中の旗は青、ゴールの旗だけ赤・大型にします。
 *          線と残り記事数は画面のUIで描き、紙が倒れ始めたら消します。
*/
class GoalRouteEffect {
public:
  /** @brief 道のりに出す内容です。*/
  struct Content {
    std::wstring startTitle;
    std::wstring goalTitle;
    int par = 0;
    int hops = 0; ///< スタートからの最短手数（0は未計算）
  };

  /**
   * @brief 紙の上に道のりを作ります。
   * @param paper 立てた直後の紙です。立っていなければ何もしません。
  */
  void Spawn(core::GameContext &ctx, const Content &content,
             ArticlePaperEffect &paper);

  /**
   * @brief 経過時間に合わせて線を伸ばし、旗を立てます。
   * @details 紙の Update より先に呼びます（紙が貼り付けた物体の位置を反映するため）。
  */
  void Update(core::GameContext &ctx, float elapsed,
              const utils::ArticleArrivalTiming &timing,
              ArticlePaperEffect &paper);

  /** @brief 生成したEntityをすべて破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  /** @brief 旗を構成する部品の役割です（CreateProceduralFlag の生成順）。*/
  enum class PartRole { Pole, Cap, Cloth, Seam };

  struct FlagPart {
    ecs::Entity entity = UINT32_MAX;
    PartRole role = PartRole::Pole;
    DirectX::XMFLOAT3 offset{}; ///< 旗の根元からの位置（旗の寸法のまま）
    DirectX::XMFLOAT3 scale{};  ///< 旗の寸法のままの大きさ
  };

  struct RouteFlag {
    utils::GoalRouteFlag route;
    DirectX::XMFLOAT3 anchor{}; ///< カップの位置（紙の座標）
    float metersPerFlagMeter = 0.0f;
    float cupWidth = 0.0f;      ///< 仮想解像度でのカップの幅
    float clothAmplitude = 0.0f;
    std::vector<FlagPart> parts;
    ecs::Entity cup = UINT32_MAX;
    ecs::Entity stepLabel = UINT32_MAX;
    ecs::Entity remainingLabel = UINT32_MAX;
  };

  /** @brief 道のりのUIの1要素です。opacity を共通で掛けます。*/
  struct UiElement {
    ecs::Entity entity = UINT32_MAX;
    float baseY = 0.0f;
  };

  ecs::Entity AddText(core::GameContext &ctx, float x, float y, float width,
                      float height, int layer);
  void CreateFlag(core::GameContext &ctx, const utils::GoalRouteFlag &route,
                  ArticlePaperEffect &paper);
  void CreateBall(core::GameContext &ctx, ArticlePaperEffect &paper);
  void CreateLabels(core::GameContext &ctx, const Content &content);
  void UpdateFlag(core::GameContext &ctx, RouteFlag &flag, float elapsed,
                  float opacity, float windYaw, ArticlePaperEffect &paper);
  void SetTextOpacity(core::GameContext &ctx, ecs::Entity entity,
                      float opacity);

  ecs::EntityOwner m_entityOwner;
  std::vector<RouteFlag> m_flags;
  std::vector<ecs::Entity> m_lineSegments; ///< 実線なら1本、未計算なら点線の各片
  std::vector<float> m_lineSegmentStarts;  ///< 各片の始点（仮想解像度）
  bool m_dashedLine = false;
  ecs::Entity m_ball = UINT32_MAX;
  DirectX::XMFLOAT3 m_ballAnchor{};
  float m_ballMetersPerPixel = 0.0f;
  ecs::Entity m_detail = UINT32_MAX;
  ecs::Entity m_startTitle = UINT32_MAX;
  ecs::Entity m_startLabel = UINT32_MAX;
  ecs::Entity m_goalHeading = UINT32_MAX;
  ecs::Entity m_goalTitle = UINT32_MAX;
  float m_goalHeadingY = 0.0f;
  float m_goalTitleY = 0.0f;
  bool m_active = false;
};

} // namespace game::controllers
