#pragma once
/**
 * @file AimDistancePanel.h
 * @brief エイムピンまでの距離を縦グラフで示すHUDパネル
 * @details 画面中央右に、現在クラブのフルスイング基準飛距離を満尺とした縦バーを
 *          表示する。バー右側にはエイムピン（中クリックで設置した狙い所）までの
 *          距離をマーカーで示し、パワーゲージをそのマーカーの高さで止めれば
 *          狙い通りの距離が出る、という目安として使う。
 *          エイムピンが設置されていない間、およびパワー/インパクト以外の
 *          フェーズでは非表示にする。
*/

#include "ClubSelectionPanel.h"
#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include "../../components/WikiComponents.h"

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

class AimDistancePanel {
public:
  /** @brief バー・ラベル・ピンマーカーのEntityを生成します。*/
  void Initialize(core::GameContext &ctx);

  /**
   * @brief 現在のショット状態に合わせて表示を更新します。
   * @param phase 現在のショットフェーズ
   * @param currentPower パワーゲージの現在位置 (0.0〜1.0)
   * @param confirmedPower 確定済みパワー (0.0〜1.0, 未確定は0.0)
   * @param aimPin 設置中のエイムピン状態（nullptrまたはactive=falseなら非表示）
   * @param currentClub 現在選択中クラブの表示用データ
*/
  void Update(core::GameContext &ctx, game::components::ShotState::Phase phase,
              float currentPower, float confirmedPower,
              const game::components::AimPinState *aimPin,
              const ClubUIData &currentClub);

  /** @brief パネル全体の表示状態を変更します。*/
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief 生成したすべてのEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  struct Entities {
    ecs::Entity track = UINT32_MAX;      ///< バー背景（枠）
    ecs::Entity fill = UINT32_MAX;       ///< 現在のパワー位置までの塗り
    ecs::Entity maxLabel = UINT32_MAX;   ///< 上端: クラブの最大飛距離
    ecs::Entity valueLabel = UINT32_MAX; ///< 塗り上端に追従する現在距離
    ecs::Entity pinLine = UINT32_MAX;    ///< ピン距離の高さを示す横線
    ecs::Entity pinMarker = UINT32_MAX;  ///< バー右のピン形マーカー（画像）
    ecs::Entity pinLabel = UINT32_MAX;   ///< ピンまでの距離テキスト
  };

  Entities m_entities;
  ecs::EntityOwner m_entityOwner;
  bool m_manuallyHidden = false; ///< SetVisible(false)中はUpdateでも表示しない
};

} // namespace game::controllers::hud
