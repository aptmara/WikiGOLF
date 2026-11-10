#pragma once
/**
 * @file TutorialGuidePanel.h
 * @brief チュートリアルの案内パネル（画面左の「目次」とHUD部品の枠）
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include "../../utils/TutorialGuideRules.h"
#include <array>
#include <cstdint>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

/**
 * @brief チュートリアルの案内を、Wikipedia の目次ボックスの形で表示します。
 * @details 章を番号つきで並べ、済んだ章は訪問済みリンクの紫、これからの章は
 *          リンクの青で書きます。いまの章だけを開き、その下に「いますること」
 *          「補足」「使うキー」「見る場所」を出します。題の行の［隠す］を
 *          クリックすると、いますることの1行だけに縮めます。
*/
class TutorialGuidePanel {
public:
  /** @brief 段階を終えた合図を出しておく秒数。進行側の待ち時間と共有します。*/
  static constexpr float kStepClearSeconds = 0.55f;

  void Initialize(core::GameContext &ctx);
  void Shutdown(core::GameContext &ctx);

  /** @brief 表示内容を差し替えます。同じ内容なら何もしません。*/
  void SetContent(core::GameContext &ctx,
                  const game::utils::TutorialGuideContent &content);

  /** @brief 枠で示すHUD部品を切り替えます。*/
  void SetSpotlight(core::GameContext &ctx,
                    game::utils::TutorialSpotlight spotlight);

  /** @brief 段階を終えた合図（節番号を緑の ✓ に替える）を出します。*/
  void PlayStepClear(core::GameContext &ctx);

  /** @brief クリックの受け付けと、表示の切り替わりの動きを進めます。*/
  void Update(core::GameContext &ctx);

  void SetVisible(core::GameContext &ctx, bool visible);

  /**
   * @brief このフレームのマウス操作をパネルが使ったかを返します。
   * @details パネルの上で押したボタンは、離すまでパネルのものとして扱います。
   *          そのあいだはカメラ・ショット・ピン置き・マップの移動へ渡しません。
  */
  bool IsCapturingPointer() const { return m_capturingPointer; }

  /** @brief いま縮めた表示か*/
  bool IsCollapsed() const { return m_collapsed; }

private:
  static constexpr std::size_t kMaxKeyRows = 3;

  struct KeyRowEntities {
    ecs::Entity joiner = UINT32_MAX;  ///< 直前とのつなぎ（→ など）
    ecs::Entity key = UINT32_MAX;     ///< キーの名前（リンクの青）
    ecs::Entity caption = UINT32_MAX; ///< その操作で起きること
  };

  struct ChapterRowEntities {
    ecs::Entity number = UINT32_MAX; ///< 章番号
    ecs::Entity title = UINT32_MAX;  ///< 章の名前
  };

  struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool Contains(float px, float py) const {
      return width > 0.0f && px >= x && px < x + width && py >= y &&
             py < y + height;
    }
  };

  ecs::Entity CreateText(core::GameContext &ctx, int layerOffset);
  void HideAll(core::GameContext &ctx);
  void Layout(core::GameContext &ctx);
  /** @brief 開いた目次を並べ、下端のy座標を返します。*/
  float LayoutExpanded(core::GameContext &ctx, bool compact);
  /** @brief 縮めた表示を並べ、下端のy座標を返します。*/
  float LayoutCollapsed(core::GameContext &ctx);
  void Place(core::GameContext &ctx, ecs::Entity entity, float x, float y,
             float width, float height);
  void ApplyOpacity(core::GameContext &ctx);
  void ApplyColors(core::GameContext &ctx);
  void UpdateSpotlight(core::GameContext &ctx);
  void UpdatePointer(core::GameContext &ctx);

  ecs::EntityOwner m_entityOwner;
  std::vector<ecs::Entity> m_texts; ///< このパネルが持つ全テキスト部品
  ecs::Entity m_background = UINT32_MAX;
  ecs::Entity m_title = UINT32_MAX;
  ecs::Entity m_toggle = UINT32_MAX;
  std::array<ChapterRowEntities, game::utils::kTutorialChapterCount>
      m_chapters{};
  ecs::Entity m_moreAbove = UINT32_MAX; ///< 縮めた目次で、上に章があることを示す「…」
  ecs::Entity m_moreBelow = UINT32_MAX; ///< 縮めた目次で、下に章があることを示す「…」
  ecs::Entity m_sectionRule = UINT32_MAX; ///< 開いた節の左の細い線
  ecs::Entity m_sectionNumber = UINT32_MAX;
  ecs::Entity m_headline = UINT32_MAX;
  ecs::Entity m_detail = UINT32_MAX;
  std::array<KeyRowEntities, kMaxKeyRows> m_keyRows{};
  ecs::Entity m_lookAt = UINT32_MAX; ///< 「見る場所：ミニマップ [1]」
  ecs::Entity m_hint = UINT32_MAX;
  ecs::Entity m_collapsedLine = UINT32_MAX; ///< 縮めたときの、いますること
  ecs::Entity m_spotlight = UINT32_MAX;
  ecs::Entity m_spotlightTag = UINT32_MAX; ///< 枠の角に付ける「[1]」

  game::utils::TutorialGuideContent m_content;
  game::utils::TutorialSpotlight m_spotlightTarget =
      game::utils::TutorialSpotlight::None;
  std::vector<ecs::Entity> m_placed; ///< いまの内容で表示している部品
  Rect m_panelRect;  ///< パネル全体（クリックを受ける範囲）
  Rect m_toggleRect; ///< 押すと開閉する範囲
  float m_enterTimer = 0.0f; ///< 内容が替わってからの秒数
  bool m_stepCleared = false;
  bool m_collapsed = false;
  bool m_toggleHovered = false;
  bool m_pressOnPanel = false;  ///< パネルの上でボタンを押し、まだ離していない
  bool m_pressOnToggle = false; ///< その押下が開閉の範囲で始まった
  bool m_capturingPointer = false;
  bool m_visible = true;
  bool m_initialized = false;
};

} // namespace game::controllers::hud
