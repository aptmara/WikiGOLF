#pragma once
/**
 * @file ShotGaugePanel.h
 * @brief ショット入力の二段階ゲージと判定表示を管理するHUDパネル
*/

#include "ClubSelectionPanel.h"
#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include "../../components/WikiComponents.h"
#include <DirectXMath.h>
#include <string>

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

/**
 * @brief パワー入力からインパクト確定後のフェードまでを管理します。
 * @details フェーズ遷移と保持時間はこのクラスだけが所有し、表示Entityと
 *          状態機械の更新順序が分散しないようにします。
*/
class ShotGaugePanel {
public:
  /** @brief ゲージパネルと判定表示を生成します。*/
  void Initialize(core::GameContext &ctx);

  /** @brief 現在のショットフェーズを表示へ反映します。*/
  void Update(core::GameContext &ctx, float deltaTime,
              game::components::ShotState::Phase phase, float currentPower,
              float confirmedPower, float currentImpact,
              float confirmedImpact, const ClubUIData &currentClub);

  /** @brief 外部で計算されたゲージ位置を0～1へ正規化します。*/
  void UpdatePowerGauge(core::GameContext &ctx, float fillValue,
                        float markerValue, float minPower, float maxPower);

  /** @brief 判定文字と意味色を表示します。*/
  void UpdateJudge(core::GameContext &ctx, const std::wstring &text,
                   const DirectX::XMFLOAT4 &color);

  /** @brief ゲージと判定を次のショット用の状態へ戻します。*/
  void Reset(core::GameContext &ctx);

  /** @brief ゲージ本体の表示状態を直接変更します。*/
  void SetGaugeVisible(core::GameContext &ctx, bool visible);

  /** @brief インパクト判定帯の表示状態を直接変更します。*/
  void SetImpactZonesVisible(core::GameContext &ctx, bool visible);

  /** @brief 通常時とショット入力時のパネル表示を切り替えます。*/
  void SetShotPhaseVisible(core::GameContext &ctx, bool shotPhase);

  /** @brief HUD全体を隠すときにショット表示を確実に隠します。*/
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief 生成したすべてのEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  struct Entities {
    ecs::Entity background = UINT32_MAX;
    ecs::Entity step = UINT32_MAX;
    ecs::Entity title = UINT32_MAX;
    ecs::Entity hint = UINT32_MAX;
    ecs::Entity powerLabel = UINT32_MAX;
    ecs::Entity powerValue = UINT32_MAX;
    ecs::Entity accuracyLabel = UINT32_MAX;
    ecs::Entity accuracyValue = UINT32_MAX;
    ecs::Entity club = UINT32_MAX;
    ecs::Entity gauge = UINT32_MAX;
    ecs::Entity judge = UINT32_MAX;
  };

  void UpdatePanelContent(core::GameContext &ctx,
                          game::components::ShotState::Phase phase,
                          float currentPower, float confirmedPower,
                          float currentImpact, float confirmedImpact,
                          const ClubUIData &currentClub);

  Entities m_entities;
  ecs::EntityOwner m_entityOwner;
  float m_phaseTransition = 1.0f;
  game::components::ShotState::Phase m_previousPhase =
      game::components::ShotState::Phase::Idle;
  float m_dismissRemaining = 0.0f;
};

} // namespace game::controllers::hud
