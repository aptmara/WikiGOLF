#pragma once
/**
 * @file StartGoalIntroductionPanel.h
 * @brief ラウンド開始時にスタート記事とゴール記事を紹介するWiki風パネル
*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include <cstddef>
#include <string>
#include <vector>

struct ID3D11ShaderResourceView;

namespace core {
struct GameContext;
}

namespace graphics {
struct TextStyle;
}

namespace game::controllers {

/**
 * @brief スタート/ゴール記事の紹介カードを生成し、登場・退場アニメーションを管理します。
 * @details インゲームHUDと同じ紙面パネル・リンク青・秀逸な記事の金色だけで構成します。
 *          カードの高さは記事名と概要の実測高さから決め、文章が枠に隠れないようにします。
*/
class StartGoalIntroductionPanel {
public:
  /** @brief パネルに表示する内容です。*/
  struct Content {
    std::wstring startTitle;    ///< スタート記事のタイトル
    std::wstring startAbstract; ///< スタート記事の概要
    std::wstring goalTitle;     ///< ゴール記事のタイトル
    int hops = 0;               ///< スタートからゴールまでの最短手数（0は不明）
    ID3D11ShaderResourceView *goalThumbnail = nullptr; ///< ゴール記事の代表画像
    float goalThumbnailAspect = 1.0f;                  ///< 代表画像の幅/高さ
  };

  /** @brief パネルのUI Entityを生成し、演出を先頭から開始します。*/
  void Initialize(core::GameContext &ctx, const Content &content);

  /**
   * @brief ゴール記事の概要を差し替えます（非同期取得の完了時に呼びます）。
   * @param rawExtract 取得した記事本文です。空なら取得失敗として扱います。
  */
  void SetGoalExtract(core::GameContext &ctx, const std::wstring &rawExtract);

  /**
   * @brief 演出を進めます。
   * @param advanceRequested プレイヤーが「次へ」を入力したフレームなら true です。
   * @return フェードアウトまで完了したら true を返します。
  */
  bool Update(core::GameContext &ctx, float dt, bool advanceRequested);

  /** @brief パネルが生成したEntityをすべて破棄します。*/
  void Shutdown(core::GameContext &ctx);

  /** @brief パネルが生成済みかどうかです。*/
  bool IsInitialized() const { return m_initialized; }

private:
  enum class Group { Veil, Header, StartCard, Arrow, GoalCard, Hint };

  struct Element {
    ecs::Entity entity = UINT32_MAX;
    Group group = Group::Veil;
    float baseY = 0.0f;
    bool isImage = false;
  };

  /** @brief 1枚の記事カードを構成するEntityです。*/
  struct Card {
    ecs::Entity panel = UINT32_MAX;
    ecs::Entity accentBar = UINT32_MAX;
    ecs::Entity label = UINT32_MAX;
    ecs::Entity title = UINT32_MAX;
    ecs::Entity rule = UINT32_MAX;
    ecs::Entity body = UINT32_MAX;
    ecs::Entity url = UINT32_MAX;
    ecs::Entity thumbnailFrame = UINT32_MAX;
    ecs::Entity thumbnail = UINT32_MAX;
    float x = 0.0f;
    float thumbnailWidth = 0.0f;
    float thumbnailHeight = 0.0f;
    std::wstring abstractText; ///< 整形済みの概要（再レイアウト時に文字数を削る元）
  };

  ecs::Entity AddText(core::GameContext &ctx, Group group, float x,
                      float width, int layer);
  ecs::Entity AddImage(core::GameContext &ctx, Group group, float x,
                       float width, float height, int layer,
                       ID3D11ShaderResourceView *texture);
  void CreateArticleCard(core::GameContext &ctx, Card &card, Group group,
                         float x, bool isGoal, const std::wstring &title);
  /** @brief 実測したテキスト高さから、カード・矢印・案内の位置と大きさを決め直します。*/
  void Layout(core::GameContext &ctx);
  float MeasureHeight(core::GameContext &ctx, const std::wstring &text,
                      const graphics::TextStyle &style, float width) const;
  float BodyWidth(const Card &card) const;
  void PlaceText(core::GameContext &ctx, ecs::Entity entity, float y,
                 float height);
  void ApplyAnimation(core::GameContext &ctx);

  std::vector<Element> m_elements;
  ecs::EntityOwner m_entityOwner;
  Card m_startCard;
  Card m_goalCard;
  ecs::Entity m_arrowCaption = UINT32_MAX;
  ecs::Entity m_arrow = UINT32_MAX;
  ecs::Entity m_hops = UINT32_MAX;
  ecs::Entity m_hint = UINT32_MAX;
  float m_elapsed = 0.0f;
  float m_fadeOutElapsed = -1.0f; ///< 負ならフェードアウト前
  bool m_initialized = false;
};

} // namespace game::controllers
