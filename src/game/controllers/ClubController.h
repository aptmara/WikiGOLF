#pragma once
/**
 * @file ClubController.h
 * @brief WikiGolfのクラブ選択UIとクラブ演出を管理するコントローラー*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
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
  float m_clubSwingAngle = 0.0f;
  float m_clubSwingSpeed = 0.0f;
  float m_clubAnimTimer = 0.0f;
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers
