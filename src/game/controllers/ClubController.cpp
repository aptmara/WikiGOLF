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

  if (ctx.input.GetKeyUp('E')) {
    if (!m_clubUIExpanded) {
      ExpandClubUI(ctx);
    } else {
      result.clubChanged = SwitchClub(ctx, 1);
      m_clubExpandTimer = kClubAutoCollapseTime;
    }
  } else if (ctx.input.GetKeyUp('Q')) {
    if (!m_clubUIExpanded) {
      ExpandClubUI(ctx);
    } else {
      result.clubChanged = SwitchClub(ctx, -1);
      m_clubExpandTimer = kClubAutoCollapseTime;
    }
  }

  if (m_clubUIExpanded) {
    m_clubExpandTimer -= ctx.dt;
    if (m_clubExpandTimer <= 0.0f) {
      CollapseClubUI(ctx);
    }
  }

  if (ctx.input.GetMouseButtonDown(0) && m_clubUIExpanded) {
    float mx = static_cast<float>(ctx.input.GetMousePosition().x);
    float my = static_cast<float>(ctx.input.GetMousePosition().y);

    for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
      auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i]);
      if (!ui || !ui->visible) {
        continue;
      }

      if (mx >= ui->x && mx <= ui->x + ui->width && my >= ui->y &&
          my <= ui->y + ui->height) {
        result.uiClicked = true;
        result.clubChanged = SelectClubByIndex(ctx, i);
        CollapseClubUI(ctx);
        break;
      }
    }
  }

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
  const float clubOffsetY = 0.3f;
  const float clubOffsetZ = -0.3f;

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
        m_clubAnimPhase = ClubAnimPhase::Idle;
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
        m_clubAnimPhase = ClubAnimPhase::Idle;
      }
    } else {
      clubMr->isVisible = false;
    }
    break;

  default:
    clubMr->isVisible = false;
    break;
  }

  float pitchRad = XMConvertToRadians(m_clubSwingAngle);
  XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitchRad, yaw, 0);
  XMStoreFloat4(&clubTr->rotation, q);

  float clubModelLength = 2.0f;
  float scaledLen = clubModelLength * clubTr->scale.y;
  XMVECTOR gripOffset = XMVectorSet(0.0f, scaledLen, 0.0f, 0.0f);
  gripOffset = XMVector3Rotate(gripOffset, q);

  XMVECTOR headPos = XMVectorSubtract(clubBasePos, gripOffset);
  XMStoreFloat3(&clubTr->position, headPos);
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

  m_availableClubs.push_back(
      {"Driver", 65.0f, 12.0f, "icon_driver.png", 3.0f});
  m_availableClubs.push_back({"Iron", 48.0f, 18.0f, "icon_iron.png", 1.30f});
  m_availableClubs.push_back({"Wedge", 35.0f, 26.0f, "icon_wedge.png", 2.5f});
  m_availableClubs.push_back(
      {"Putter", 10.0f, 0.0f, "icon_putter.png", 1.00f});

  m_currentClubIndex = 0;
  m_currentClub = m_availableClubs[0];
  m_clubUIExpanded = false;
  m_clubExpandTimer = 0.0f;

  constexpr float kClubX = 14.0f;
  constexpr float kClubStartY = 120.0f;
  constexpr float kClubSpacing = 88.0f;
  constexpr float kClubSize = 72.0f;

  for (size_t i = 0; i < m_availableClubs.size(); ++i) {
    auto e = ctx.world.CreateEntity();
    auto &img = ctx.world.Add<UIImage>(e);
    img = UIImage::Create(m_availableClubs[i].iconTexture, 0, 0);
    img.x = kClubX;
    img.y = kClubStartY + static_cast<float>(i) * kClubSpacing;
    img.width = kClubSize;
    img.height = kClubSize;
    img.layer = 20;

    if (i == 0) {
      img.alpha = 1.0f;
      img.visible = true;
    } else {
      img.alpha = 0.5f;
      img.visible = false;
    }

    m_clubUIEntities.push_back(e);

    auto nameE = ctx.world.CreateEntity();
    auto &nameT = ctx.world.Add<UIText>(nameE);
    nameT.text = core::ToWString(m_availableClubs[i].name);
    nameT.x = kClubX + kClubSize + 6.0f;
    nameT.y = kClubStartY + static_cast<float>(i) * kClubSpacing +
              kClubSize * 0.3f;
    nameT.width = 100.0f;
    nameT.height = 24.0f;
    nameT.style = graphics::TextStyle::ClubName();
    nameT.style.fontSize = 14.0f;
    nameT.style.align = graphics::TextAlign::Left;
    nameT.visible = false;
    nameT.layer = 21;
    m_clubNameEntities.push_back(nameE);
  }

  auto qIconE = ctx.world.CreateEntity();
  auto &qImg = ctx.world.Add<UIImage>(qIconE);
  qImg = UIImage::Create("Assets/ui/keyboard_q.png",
                         kClubX + kClubSize / 2 - 20.0f, kClubStartY - 24.0f);
  qImg.width = 18.0f;
  qImg.height = 18.0f;
  qImg.layer = 21;
  qImg.visible = true;

  auto eIconE = ctx.world.CreateEntity();
  auto &eImg = ctx.world.Add<UIImage>(eIconE);
  eImg = UIImage::Create("Assets/ui/keyboard_e.png",
                         kClubX + kClubSize / 2 + 2.0f, kClubStartY - 24.0f);
  eImg.width = 18.0f;
  eImg.height = 18.0f;
  eImg.layer = 21;
  eImg.visible = true;
}

void ClubController::InitializeClubModel(core::GameContext &ctx) {
  m_clubModelEntity = ctx.world.CreateEntity();

  auto &tr = ctx.world.Add<Transform>(m_clubModelEntity);
  tr.position = {0.0f, 0.5f, 0.0f};
  tr.scale = {0.5f, 0.5f, 0.5f};

  auto &mr = ctx.world.Add<MeshRenderer>(m_clubModelEntity);
  mr.mesh = ctx.resource.LoadMesh("Assets/models/golf_club.fbx");
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
