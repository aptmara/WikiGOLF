#pragma once
/**
 * @file ClubController.h
 * @brief WikiGolfのクラブ選択UIとクラブ演出を管理するコントローラー*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include "../../graphics/SkeletalModel.h"
#include "../../resources/ResourceManager.h"
#include "../utils/CarryDistanceTable.h"
#include "TrajectoryPredictor.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::components {
struct ShotState;
}

namespace game::controllers {

/**
 * @brief クラブ定義・クラブUI・クラブモデル演出をまとめて管理する
*/
class ClubController {
public:
  struct Club {
    std::string name;
    float maxPower;
    float launchAngle;
    std::string iconTexture;
    float rollingFrictionScale = 1.0f;
    std::string shortName;   // e.g. "1W"
    std::string categoryEN;  // e.g. "Driver"

    /**
     * @brief 平坦・無風フェアウェイでのフルスイング基準キャリー飛距離(ヤード)。
     * Initialize時にmaxPower/launchAngleから動的に算出する。
*/
    float baseCarryDistance = 0.0f;
    /** @brief 「目標飛距離→初速」を求めるための対応表(基準飛距離と同時に算出)。*/
    game::utils::CarryDistanceTable carryTable;
  };

  struct InputParams {
    bool allowInput = true;
  };

  struct InputResult {
    bool uiClicked = false;
    bool clubChanged = false;
  };

  void Initialize(core::GameContext &ctx);

  /**
   * @brief クラブ表示が生成したEntityを破棄します。
   * @param ctx ゲーム全体の共有コンテキストです。
*/
  void Shutdown(core::GameContext &ctx);
  InputResult UpdateInput(core::GameContext &ctx, const InputParams &params);
  void UpdateAnimation(core::GameContext &ctx, float dt, ecs::Entity ballEntity,
                       const DirectX::XMFLOAT3 &shotDirection);

  const Club &GetCurrentClub() const;
  int GetCurrentClubIndex() const;

  /**
   * @brief 指定した目標飛距離に最も近い基準飛距離のクラブへ切り替えます。
   * @details エイムピン設置時に、狙った距離へ最も飛ばしやすいクラブを
   *          「いい感じに」自動選択するために使う。
   * @param targetDistance 目標飛距離(ヤード相当)
   * @return クラブが1本以上あり切り替えを行った場合true
*/
  bool SelectClubForDistance(core::GameContext &ctx, float targetDistance);

  /**
   * @brief 先頭のクラブ（ドライバー）へ選択を戻します。
   * @details 次のステージ（記事ページ）へ遷移した際に、前のホールで使っていた
   *          クラブ（パター等）を持ち越さないようにするために使う。
   * @return クラブが1本以上あり切り替えを行った場合true
  */
  bool ResetToFirstClub(core::GameContext &ctx);
  float GetRecommendedCameraDistance(float fieldScale) const;
  float GetRecommendedCameraHeight(float fieldScale) const;

  /** @brief 全クラブ名リストを返す (WikiGolfHUD のクラブ選択リスト描画用)*/
  const std::vector<Club>& GetAllClubs() const { return m_availableClubs; }

  /** @brief ゴルファー(ロボット)の現在の立ち位置（足元）*/
  const DirectX::XMFLOAT3 &GetGolferPosition() const { return m_golferStandPos; }

  /** @brief ゴルファーのワールド上の身長（カメラ配置の基準）*/
  float GetGolferHeight() const;

  /**
   * @brief スイング(Hit)開始からクラブが最下点に達するまでの秒数
   * @details シーン側はインパクト確定からこの秒数後にボールを発射する。
  */
  float GetImpactDelay() const;

  /**
   * @brief カップイン時の喜び演出を開始する（ポールの横まで歩いてくる）
   * @param cameraPos 現在のカメラ位置（ポールのどちら側に立つか決めるのに使う）
   * @return ゴルファーが喜びモーションを行う立ち位置
  */
  DirectX::XMFLOAT3 BeginCelebration(core::GameContext &ctx,
                                     const DirectX::XMFLOAT3 &holePos,
                                     const DirectX::XMFLOAT3 &cameraPos);

  /** @brief 喜び演出の更新（到着後はカメラの方を向いてCelebrateを再生）*/
  void UpdateCelebration(core::GameContext &ctx, float dt,
                         const DirectX::XMFLOAT3 &cameraPos);

  /** @brief 喜びモーションを最後まで再生し終えたか*/
  bool IsCelebrationFinished() const {
    return m_celebrationStage == CelebrationStage::Done;
  }

  /** @brief 喜び演出を終了し、次のコースで立ち位置を取り直す状態に戻す*/
  void EndCelebration();

private:
  enum class ClubAnimPhase {
    Idle,
    Backswing,
    Downswing,
    FollowThrough,
    Finished
  };

  void InitializeClubs(core::GameContext &ctx);
  void InitializeClubModel(core::GameContext &ctx);
  bool SwitchClub(core::GameContext &ctx, int direction);
  void ExpandClubUI(core::GameContext &ctx);
  void CollapseClubUI(core::GameContext &ctx);
  bool SelectClubByIndex(core::GameContext &ctx, size_t index);

  /**
   * @brief 現在のクラブ種別に応じたスイングフェーズ別クリップ名を返す
   * @details パターは"Putt_"、それ以外は"FullSwing_"を接頭辞とし、
   *          suffixには"Charge"(溜め)/"Hold"(維持)/"Hit"(打つ・振り抜き)を渡す。
  */
  std::string GetPhaseClipName(const char *suffix) const;

  /**
   * @brief 現在のクリップ（と切替元クリップとのクロスフェード）で
   *        スキンメッシュの頂点を再計算しGPUへ送る
  */
  void UpdateGolferPose(core::GameContext &ctx);

  /**
   * @brief 再生クリップを切り替え/進める
   * @details 切替時は素材の遷移仕様に合わせてクロスフェードする
   *          (Charge→Holdは0秒、Hold→Hitは80ms、それ以外は0.2秒)。
  */
  void AdvanceClip(const std::string &clipName, bool loop, float dt);

  float ClipDuration(const std::string &clipName) const;

  std::vector<Club> m_availableClubs;
  Club m_currentClub = {"Driver", 30.0f, 30.0f, "icon_driver.png", 1.0f};
  int m_currentClubIndex = 0;

  std::vector<ecs::Entity> m_clubUIEntities;
  std::vector<ecs::Entity> m_clubNameEntities;
  bool m_clubUIExpanded = false;
  float m_clubExpandTimer = 0.0f;
  static constexpr float kClubAutoCollapseTime = 3.5f;

  ecs::Entity m_clubModelEntity = UINT32_MAX;
  ClubAnimPhase m_clubAnimPhase = ClubAnimPhase::Idle;
  float m_clubAnimTimer = 0.0f;
  ecs::EntityOwner m_entityOwner;

  // --- G01_Robot_Golfer スケルタルアニメーション ---
  graphics::SkeletalModel m_golferModel;
  resources::MeshHandle m_golferMeshHandle;
  std::vector<graphics::Vertex> m_golferPoseScratch;
  std::string m_currentClipName = "Idle";
  float m_clipTime = 0.0f;
  bool m_golferModelValid = false;

  // ゴルファーが構えるスタンス位置（ボール基準のローカルオフセット）とヨー補正。
  // 実機で見た目を確認しながら調整することを想定した定数。
  static constexpr float kGolferStanceOffsetX = -0.9f;
  static constexpr float kGolferStanceOffsetZ = -0.4f;
  static constexpr float kGolferYawOffsetDeg = 90.0f;
  static constexpr float kGolferScale = 10.0f;

  // ボールの位置が変わったら歩いて移動する（毎フレームボールに追従しないための状態）
  bool m_golferPositionInitialized = false;
  DirectX::XMFLOAT3 m_golferStandPos = {0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 m_golferWalkTarget = {0.0f, 0.0f, 0.0f};
  bool m_golferWalking = false;
  static constexpr float kGolferWalkSpeed = 2.5f; // units/sec
  static constexpr float kGolferWalkTriggerDistance = 0.5f;
  // これ以上の大移動（新ホール開始など、カメラに映っていない場面）は歩かず瞬間移動する
  static constexpr float kGolferTeleportDistance = 15.0f;

  // クリップ切替のクロスフェード状態
  std::string m_prevClipName;
  float m_prevClipTime = 0.0f;
  float m_crossfadeTimer = 0.0f;
  float m_crossfadeDuration = 0.0f;
  bool m_chargeFinished = false; // 溜め(Charge)を最後まで再生し終えたか

  // Hitクリップ開始からクラブ最下点までの秒数（素材仕様）
  static constexpr float kFullSwingImpactSeconds = 0.5f;
  static constexpr float kPuttImpactSeconds = 0.3f;

  // --- カップイン喜び演出 ---
  enum class CelebrationStage { None, Walking, Celebrating, Done };
  CelebrationStage m_celebrationStage = CelebrationStage::None;
  DirectX::XMFLOAT3 m_celebrationSpot = {0.0f, 0.0f, 0.0f};
  // 立ち位置はポールからカメラ側へ/横へ、身長比でずらす（ポールと重ならないように）
  static constexpr float kCelebrateFrontRatio = 0.5f;
  static constexpr float kCelebrateSideRatio = 0.8f;
  // これより遠くにいる場合は、画面外(横方向)から歩いて入ってくる位置へ瞬間移動する
  static constexpr float kCelebrateMaxWalkRatio = 4.0f;
  static constexpr float kCelebrateWalkInRatio = 3.0f;
  // Celebrate再生後、遷移するまで余韻として待つ秒数
  static constexpr float kCelebrateHoldSeconds = 0.4f;
};

} // namespace game::controllers
