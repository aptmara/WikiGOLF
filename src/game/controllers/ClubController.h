#pragma once
/**
 * @file ClubController.h
 * @brief WikiGolfのクラブ選択UIとクラブ演出を管理するコントローラー
 * @author 山内陽
 */

#include "../../ecs/Entity.h"
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
  };

  struct InputParams {
    bool allowInput = true;
  };

  struct InputResult {
    bool uiClicked = false;
    bool clubChanged = false;
  };

  void Initialize(core::GameContext &ctx);
  InputResult UpdateInput(core::GameContext &ctx, const InputParams &params);
  void UpdateAnimation(core::GameContext &ctx, float dt, ecs::Entity ballEntity,
                       const DirectX::XMFLOAT3 &shotDirection);

  const Club &GetCurrentClub() const;
  int GetCurrentClubIndex() const;
  float GetRecommendedCameraDistance(float fieldScale) const;
  float GetRecommendedCameraHeight(float fieldScale) const;

private:
  enum class ClubAnimPhase {
    Idle,
    Backswing,
    Downswing,
    FollowThrough
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
};

} // namespace game::controllers
