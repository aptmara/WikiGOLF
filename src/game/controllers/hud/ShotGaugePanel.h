#pragma once
/**
 * @file ShotGaugePanel.h
 * @brief ショット入力のヤード定規ゲージと判定表示を管理するHUDパネル
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

  /**
   * @brief 現在のショットフェーズを表示へ反映します。
   * @param impactCenter インパクトの「パーフェクト」中心位置 (0.0〜1.0)。
   *        パワー確定位置に連動し、定規上では確定した距離の先端に当たる。
   * @param aimPin 設置中の照準ピン。必要な距離の位置へ旗を立てる。
   * @param swingCommitted インパクトが確定し、スイング中か。立ち上がった瞬間に
   *        揺れや祝いの演出を始める。
*/
  void Update(core::GameContext &ctx, float deltaTime,
              game::components::ShotState::Phase phase, float currentPower,
              float confirmedPower, float confirmedImpact, float impactCenter,
              const ClubUIData &currentClub,
              const game::components::AimPinState *aimPin,
              bool swingCommitted);

  /** @brief 結果表示の演出中か。毎フレーム更新が必要なときにtrueを返します。*/
  bool IsResultAnimating() const { return m_resultActive; }

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

  /** @brief HUD全体を隠すときにショット表示を確実に隠します。*/
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief 生成したすべてのEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  struct Entities {
    ecs::Entity gauge = UINT32_MAX;
    ecs::Entity judge = UINT32_MAX;
  };

  /** @brief 打撃の反応に関わるタイマーを初期状態へ戻します。*/
  void ResetTimers();

  Entities m_entities;
  ecs::EntityOwner m_entityOwner;
  game::components::ShotState::Phase m_previousPhase =
      game::components::ShotState::Phase::Idle;
  float m_entranceElapsed = 99.0f;
  float m_zoneElapsed = 99.0f;
  float m_confirmElapsed = 99.0f;

  // インパクト確定の瞬間の値を控える。発射後にShotStateがリセットされても
  // 演出の最後まで同じ結果を表示できるようにする。
  bool m_resultActive = false;
  bool m_wasCommitted = false;
  float m_resultElapsed = 0.0f;
  float m_resultPower = 0.0f;
  float m_resultImpact = 0.0f;
  float m_resultCenter = 0.5f;
  int m_resultYards = 0;
  game::components::ShotJudgement m_resultJudgement =
      game::components::ShotJudgement::None;
};

} // namespace game::controllers::hud
