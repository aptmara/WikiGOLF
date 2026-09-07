#pragma once
/**
 * @file ClubSelectionPanel.h
 * @brief クラブ選択一覧と着弾予測ボタンを管理するHUDパネル
 */

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::controllers {

/** @brief クラブ選択UIへ渡す表示用データです。 */
struct ClubUIData {
  std::string name;
  std::string iconTexture;
  std::string shortName;
  std::string categoryEN;
  float maxPower = 0.0f;
  float baseCarryDistance = 0.0f;
};

namespace hud {

/**
 * @brief クラブ一覧の生成、3行窓、選択表示を一括管理します。
 * @details 表示中の行は常に「前・選択中・次」の最大3行です。
 */
class ClubSelectionPanel {
public:
  /** @brief 固定見出しと着弾予測ボタンを生成します。 */
  void Initialize(core::GameContext &ctx);

  /**
   * @brief クラブ数に合わせて行を構築し、選択位置を表示へ反映します。
   * @param elapsedTime HUD開始からの経過時間です。
   */
  void Update(core::GameContext &ctx, float elapsedTime,
              const std::vector<ClubUIData> &clubs, int currentClubIndex);

  /** @brief 着弾予測ボタンの状態を表示へ反映します。 */
  void UpdateLandingPreviewButton(core::GameContext &ctx, bool hovered,
                                  bool active, bool enabled);

  /** @brief ショット中のクラブ選択UIの表示状態を切り替えます。 */
  void SetShotPhaseVisible(core::GameContext &ctx, bool shotPhase);

  /** @brief パネル全体の表示状態を変更します。 */
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief パネルが生成したすべてのEntityを破棄します。 */
  void Shutdown(core::GameContext &ctx);

private:
  struct ClubRowEntities {
    ecs::Entity background = UINT32_MAX;
    ecs::Entity icon = UINT32_MAX;
    ecs::Entity name = UINT32_MAX;
    ecs::Entity subName = UINT32_MAX;
    bool visibleInWindow = false;
    bool selected = false;
  };

  void RebuildRows(core::GameContext &ctx,
                   const std::vector<ClubUIData> &clubs);
  void UpdateRow(core::GameContext &ctx, ClubRowEntities &row, int slot,
                 bool selected, float elapsedTime);

  ecs::Entity m_header = UINT32_MAX;
  ecs::Entity m_scrollUp = UINT32_MAX;
  ecs::Entity m_scrollDown = UINT32_MAX;
  ecs::Entity m_landingPreviewBackground = UINT32_MAX;
  ecs::Entity m_landingPreviewText = UINT32_MAX;
  std::vector<ClubRowEntities> m_rows;
  ecs::EntityOwner m_staticEntityOwner;
  ecs::EntityOwner m_rowEntityOwner;
};

} // namespace hud
} // namespace game::controllers
