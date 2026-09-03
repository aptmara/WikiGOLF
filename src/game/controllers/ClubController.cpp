#include "ClubController.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../utils/TrajectorySimulation.h"
#include <algorithm>
#include <cmath>

namespace game::controllers {

using namespace DirectX;
using namespace game::components;

void ClubController::Initialize(core::GameContext &ctx) {
  InitializeClubs(ctx);
  InitializeClubModel(ctx);
}

ClubController::InputResult
ClubController::UpdateInput(core::GameContext &ctx, const InputParams &params) {
  InputResult result;
  if (!params.allowInput) {
    return result;
  }

  // Q/E キーでクラブを切り替える（HUD 側にリスト表示を移管したため Expand 不要）
  if (ctx.input.GetKeyUp('E')) {
    result.clubChanged = SwitchClub(ctx, 1);
  } else if (ctx.input.GetKeyUp('Q')) {
    result.clubChanged = SwitchClub(ctx, -1);
  }

  // 古い UIImage クリック選択は無効化 (HUD の常時リストに移管)

  // 古い UIImage クリック選択は無効化 (HUD の常時リストに移管)

  return result;
}

void ClubController::UpdateAnimation(core::GameContext &ctx, float dt,
                                     ecs::Entity ballEntity,
                                     const XMFLOAT3 &shotDirection) {
  if (!ctx.world.IsAlive(m_clubModelEntity) || !ctx.world.IsAlive(ballEntity)) {
    return;
  }

  auto *clubTr = ctx.world.Get<Transform>(m_clubModelEntity);
  auto *clubMr = ctx.world.Get<MeshRenderer>(m_clubModelEntity);
  auto *ballTr = ctx.world.Get<Transform>(ballEntity);
  auto *shot = ctx.world.GetGlobal<ShotState>();

  if (!clubTr || !clubMr || !ballTr || !shot) {
    return;
  }

  const float clubOffsetX = -0.8f;
  const float clubOffsetY = 0.6f;
  const float clubOffsetZ = -0.3f;
  const float minClubModelY = ballTr->position.y + 0.12f;

  float yaw = std::atan2(shotDirection.x, shotDirection.z);

  XMVECTOR localOffset = XMVectorSet(clubOffsetX, clubOffsetY, clubOffsetZ, 0);
  XMMATRIX rotMatrix = XMMatrixRotationY(yaw);
  XMVECTOR worldOffset = XMVector3Transform(localOffset, rotMatrix);
  XMVECTOR clubBasePos =
      XMVectorAdd(XMLoadFloat3(&ballTr->position), worldOffset);

  switch (shot->phase) {
  case ShotState::Phase::Idle:
    clubMr->isVisible = false;
    m_clubAnimPhase = ClubAnimPhase::Idle;
    m_clubSwingAngle = 0.0f;
    break;

  case ShotState::Phase::PowerCharging: {
    clubMr->isVisible = true;
    m_clubAnimPhase = ClubAnimPhase::Backswing;
    float targetAngle = -shot->powerGaugePos * 90.0f;
    m_clubSwingAngle += (targetAngle - m_clubSwingAngle) * 8.0f * dt;
    break;
  }

  case ShotState::Phase::ImpactTiming: {
    clubMr->isVisible = true;
    float targetAngle = -shot->confirmedPower * 90.0f;
    m_clubSwingAngle += (targetAngle - m_clubSwingAngle) * 8.0f * dt;
    break;
  }

  case ShotState::Phase::Executing: {
    if (m_clubAnimPhase == ClubAnimPhase::Finished) {
      clubMr->isVisible = false;
      break;
    }

    clubMr->isVisible = true;

    if (m_clubAnimPhase != ClubAnimPhase::Downswing &&
        m_clubAnimPhase != ClubAnimPhase::FollowThrough) {
      m_clubAnimPhase = ClubAnimPhase::Downswing;
      m_clubSwingSpeed = 800.0f;
      m_clubAnimTimer = 0.0f;
    }

    if (m_clubAnimPhase == ClubAnimPhase::Downswing) {
      m_clubSwingAngle += m_clubSwingSpeed * dt;
      m_clubSwingSpeed *= 0.92f;

      if (m_clubSwingAngle >= 60.0f) {
        m_clubAnimPhase = ClubAnimPhase::FollowThrough;
        m_clubSwingAngle = 60.0f;
      }
    } else if (m_clubAnimPhase == ClubAnimPhase::FollowThrough) {
      m_clubAnimTimer += dt;
      if (m_clubAnimTimer > 0.4f) {
        m_clubSwingAngle += (0.0f - m_clubSwingAngle) * 3.0f * dt;
      }

      if (m_clubAnimTimer > 1.0f) {
        clubMr->isVisible = false;
        m_clubAnimPhase = ClubAnimPhase::Finished;
      }
    }
    break;
  }

  case ShotState::Phase::ShowResult:
    if (m_clubAnimPhase == ClubAnimPhase::FollowThrough) {
      m_clubAnimTimer += dt;
      m_clubSwingAngle += (0.0f - m_clubSwingAngle) * 3.0f * dt;
      if (m_clubAnimTimer > 1.0f) {
        clubMr->isVisible = false;
        m_clubAnimPhase = ClubAnimPhase::Finished;
      }
    } else {
      clubMr->isVisible = false;
    }
    break;

  default:
    clubMr->isVisible = false;
    break;
  }

  float pitchRad = -XMConvertToRadians(m_clubSwingAngle);
  XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitchRad, yaw, 0);
  XMStoreFloat4(&clubTr->rotation, q);

  float clubModelLength = 2.0f;
  float scaledLen = clubModelLength * clubTr->scale.y;
  XMVECTOR gripOffset = XMVectorSet(0.0f, scaledLen, 0.0f, 0.0f);
  gripOffset = XMVector3Rotate(gripOffset, q);

  XMVECTOR headPos = XMVectorSubtract(clubBasePos, gripOffset);
  XMFLOAT3 modelPos;
  XMStoreFloat3(&modelPos, headPos);
  modelPos.y = std::max(modelPos.y, minClubModelY);
  clubTr->position = modelPos;
}

const ClubController::Club &ClubController::GetCurrentClub() const {
  return m_currentClub;
}

int ClubController::GetCurrentClubIndex() const { return m_currentClubIndex; }

float ClubController::GetRecommendedCameraDistance(float fieldScale) const {
  if (m_currentClub.name == "Putter") {
    return 4.0f * fieldScale;
  }
  if (m_currentClub.name == "Wedge") {
    return 10.0f * fieldScale;
  }
  return 15.0f * fieldScale;
}

float ClubController::GetRecommendedCameraHeight(float fieldScale) const {
  if (m_currentClub.name == "Putter") {
    return 8.0f * fieldScale;
  }
  if (m_currentClub.name == "Wedge") {
    return 6.0f * fieldScale;
  }
  return 5.0f * fieldScale;
}

void ClubController::InitializeClubs(core::GameContext &ctx) {
  m_availableClubs.clear();
  m_clubUIEntities.clear();
  m_clubNameEntities.clear();

  m_availableClubs.push_back({"ドライバー", 130.0f, 12.0f, "Assets/textures/Club_01_1W_Driver.png", 3.0f, "1W", "Driver"});
  m_availableClubs.push_back({"3W", 115.0f, 14.0f, "Assets/textures/Club_02_3W_Wood.png", 2.5f, "3W", "Wood"});
  m_availableClubs.push_back({"5W", 100.0f, 16.0f, "Assets/textures/Club_03_5W_Wood.png", 2.0f, "5W", "Wood"});
  m_availableClubs.push_back({"5I", 85.0f, 20.0f, "Assets/textures/Club_04_5I_Iron.png", 1.5f, "5I", "Iron"});
  m_availableClubs.push_back({"7I", 70.0f, 24.0f, "Assets/textures/Club_05_7I_Iron.png", 1.2f, "7I", "Iron"});
  m_availableClubs.push_back({"9I", 55.0f, 28.0f, "Assets/textures/Club_06_9I_Iron.png", 1.0f, "9I", "Iron"});
  m_availableClubs.push_back({"PW", 40.0f, 32.0f, "Assets/textures/Club_07_PW_PitchingWedge.png", 1.5f, "PW", "Wedge"});
  m_availableClubs.push_back({"SW", 25.0f, 38.0f, "Assets/textures/Club_08_SW_SandWedge.png", 2.5f, "SW", "Wedge"});
  m_availableClubs.push_back({"パター", 10.0f, 0.0f, "Assets/textures/Club_09_PT_Putter.png", 1.0f, "PT", "Putter"});

  // 各クラブの基準飛距離(平坦・無風フェアウェイ基準)を動的に算出する。
  // ExecuteShotとTrajectoryPredictorはこの表を通じて「目標飛距離→初速」を
  // 逆引きするため、maxPower/launchAngleを調整すればここも自動で追従する。
  for (auto &club : m_availableClubs) {
    game::physics::BallPhysicsParams ballParams;
    ballParams.rollingFrictionScale = club.rollingFrictionScale;
    club.carryTable = game::physics::BuildCarryDistanceTable(
        club.maxPower, club.launchAngle, ballParams);
    club.baseCarryDistance = club.carryTable.distances.empty()
                                 ? 0.0f
                                 : club.carryTable.distances.back();
  }

  m_currentClubIndex = 0;
  m_currentClub = m_availableClubs[0];
  m_clubUIExpanded = false;
  m_clubExpandTimer = 0.0f;

  constexpr float kClubX = 14.0f;
  constexpr float kClubStartY = 120.0f;
  constexpr float kClubSpacing = 88.0f;
  constexpr float kClubSize = 72.0f;

  // 古いUIエンティティは非表示のまま残し、描画はHUD側の新規リストに委譲
  for (size_t i = 0; i < m_availableClubs.size(); ++i) {
    auto e = ctx.world.CreateEntity();
    auto &img = ctx.world.Add<UIImage>(e);
    img = UIImage::Create(m_availableClubs[i].iconTexture, 0, 0);
    img.visible = false; // HUD に移管したため非表示
    img.layer = 20;
    m_clubUIEntities.push_back(e);

    auto nameE = ctx.world.CreateEntity();
    auto &nameT = ctx.world.Add<UIText>(nameE);
    nameT.text = core::ToWString(m_availableClubs[i].name);
    nameT.visible = false; // HUD に移管したため非表示
    nameT.layer = 21;
    m_clubNameEntities.push_back(nameE);
  }

  // Q/E キーアイコンも非表示 (操作ヘルプを HUD の ControlHint に移管)

  // Q/E キーアイコン: 非表示で生成 (ControlHint バーに移管)
  auto qIconE = ctx.world.CreateEntity();
  auto &qImg = ctx.world.Add<UIImage>(qIconE);
  qImg = UIImage::Create("Assets/ui/keyboard_q.png", 0, 0);
  qImg.visible = false;

  auto eIconE = ctx.world.CreateEntity();
  auto &eImg = ctx.world.Add<UIImage>(eIconE);
  eImg = UIImage::Create("Assets/ui/keyboard_e.png", 0, 0);
  eImg.visible = false;
}

void ClubController::InitializeClubModel(core::GameContext &ctx) {
  m_clubModelEntity = ctx.world.CreateEntity();

  auto &tr = ctx.world.Add<Transform>(m_clubModelEntity);
  tr.position = {0.0f, 0.5f, 0.0f};
  tr.scale = {0.5f, 0.5f, 0.5f};

  auto &mr = ctx.world.Add<MeshRenderer>(m_clubModelEntity);
  mr.mesh = ctx.resource.LoadMesh("Assets/models/golf_club.glb");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {0.9f, 0.9f, 0.9f, 1.0f};
  mr.isVisible = false;

  m_clubAnimPhase = ClubAnimPhase::Idle;
  m_clubSwingAngle = 0.0f;
  m_clubSwingSpeed = 0.0f;
  m_clubAnimTimer = 0.0f;

  LOG_INFO("WikiGolf", "Golf club model initialized");
}

bool ClubController::SwitchClub(core::GameContext &ctx, int direction) {
  if (m_availableClubs.empty()) {
    return false;
  }

  m_currentClubIndex += direction;
  if (m_currentClubIndex < 0) {
    m_currentClubIndex = static_cast<int>(m_availableClubs.size()) - 1;
  }
  if (m_currentClubIndex >= static_cast<int>(m_availableClubs.size())) {
    m_currentClubIndex = 0;
  }

  m_currentClub = m_availableClubs[m_currentClubIndex];
  if (auto *state = ctx.world.GetGlobal<GolfGameState>()) {
    state->rollingFrictionScale = m_currentClub.rollingFrictionScale;
  }

  for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
    auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i]);
    if (ui) {
      ui->alpha = (static_cast<int>(i) == m_currentClubIndex) ? 1.0f : 0.5f;
    }
  }

  LOG_INFO("WikiGolf", "Switched Club: {}", m_currentClub.name);
  return true;
}

void ClubController::ExpandClubUI(core::GameContext &ctx) {
  m_clubUIExpanded = true;
  m_clubExpandTimer = kClubAutoCollapseTime;

  for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
    if (auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i])) {
      ui->visible = true;
    }
    if (i < m_clubNameEntities.size()) {
      if (auto *name = ctx.world.Get<UIText>(m_clubNameEntities[i])) {
        name->visible = true;
      }
    }
  }
}

void ClubController::CollapseClubUI(core::GameContext &ctx) {
  m_clubUIExpanded = false;

  for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
    if (auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i])) {
      ui->visible = (static_cast<int>(i) == m_currentClubIndex);
    }
    if (i < m_clubNameEntities.size()) {
      if (auto *name = ctx.world.Get<UIText>(m_clubNameEntities[i])) {
        name->visible = false;
      }
    }
  }
}

bool ClubController::SelectClubByIndex(core::GameContext &ctx, size_t index) {
  if (index >= m_availableClubs.size()) {
    return false;
  }

  m_currentClubIndex = static_cast<int>(index);
  m_currentClub = m_availableClubs[index];
  if (auto *state = ctx.world.GetGlobal<GolfGameState>()) {
    state->rollingFrictionScale = m_currentClub.rollingFrictionScale;
  }

  for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
    auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i]);
    if (ui) {
      ui->alpha = (i == index) ? 1.0f : 0.5f;
    }
  }

  LOG_INFO("WikiGolf", "Switched to club: {}", m_currentClub.name);
  return true;
}

} // namespace game::controllers
