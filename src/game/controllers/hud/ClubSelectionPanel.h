#pragma once
/**
 * @file ClubSelectionPanel.h
 * @brief 選択中クラブと操作キーを表示するHUDパネル
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::controllers {

/** @brief クラブ選択UIへ渡す表示用データです。*/
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
 * @brief Q/E操作と選択中クラブ1本の表示を管理します。
*/
class ClubSelectionPanel {
public:
  /** @brief 固定見出しと着弾予測ボタンを生成します。*/
  void Initialize(core::GameContext &ctx);

  /**
   * @brief クラブ数に合わせて行を構築し、選択位置を表示へ反映します。
   * @param elapsedTime HUD開始からの経過時間です。
*/
  void Update(core::GameContext &ctx, float elapsedTime,
              const std::vector<ClubUIData> &clubs, int currentClubIndex);

  /** @brief ショット中のクラブ選択UIの表示状態を切り替えます。*/
  void SetShotPhaseVisible(core::GameContext &ctx, bool shotPhase);

  /** @brief パネル全体の表示状態を変更します。*/
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief パネル全体へフェード透明度を適用します。*/
  void SetOpacity(core::GameContext &ctx, float opacity);

  /** @brief パネルが生成したすべてのEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  void RebuildSelectedClub(core::GameContext &ctx, const ClubUIData &club);

  ecs::Entity m_background = UINT32_MAX;
  ecs::Entity m_qKey = UINT32_MAX;
  ecs::Entity m_eKey = UINT32_MAX;
  ecs::Entity m_pinIcon = UINT32_MAX;
  ecs::Entity m_pinHint = UINT32_MAX;
  ecs::Entity m_clubIcon = UINT32_MAX;
  ecs::Entity m_clubName = UINT32_MAX;
  ecs::Entity m_clubSubName = UINT32_MAX;
  int m_selectedIndex = -1;
  ecs::EntityOwner m_staticEntityOwner;
  ecs::EntityOwner m_selectedEntityOwner;
};

} // namespace hud
} // namespace game::controllers
