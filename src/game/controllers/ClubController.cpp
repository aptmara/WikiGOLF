/**
 * @file ClubController.cpp
 * @brief ClubController の実装
*/

#include "ClubController.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../systems/WikiTerrainSystem.h"
#include "../utils/AimPinClubSelection.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/TrajectorySimulation.h"
#include <algorithm>
#include <cmath>

namespace game::controllers {

using namespace DirectX;
using namespace game::components;

void ClubController::Initialize(core::GameContext &ctx,
                                game::systems::WikiTerrainSystem *terrainSystem) {
  m_terrainSystem = terrainSystem;
  InitializeClubs(ctx);
  InitializeClubModel(ctx);
}

void ClubController::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_clubModelEntity = UINT32_MAX;
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

  return result;
}

std::string ClubController::GetPhaseClipName(const char *suffix) const {
  const bool isPutter = m_currentClub.categoryEN == "Putter";
  return (isPutter ? std::string("Putt_") : std::string("FullSwing_")) + suffix;
}

float ClubController::ClipDuration(const std::string &clipName) const {
  const graphics::AnimationClip *clip = m_golferModel.FindClip(clipName);
  return clip ? clip->duration : 0.0f;
}

float ClubController::GetGolferHeight() const {
  if (!m_golferModelValid) {
    return 2.0f;
  }
  return m_golferModel.GetBindPoseHeight() * kGolferScale;
}

float ClubController::GetImpactDelay() const {
  return m_currentClub.categoryEN == "Putter" ? kPuttImpactSeconds
                                              : kFullSwingImpactSeconds;
}

namespace {

bool EndsWith(const std::string &s, const char *suffix) {
  const size_t n = std::char_traits<char>::length(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/** @brief 素材の遷移仕様に合わせたクロスフェード秒数*/
float CrossfadeSeconds(const std::string &from, const std::string &to) {
  if (from.empty()) {
    return 0.0f;
  }
  if (EndsWith(from, "_Charge") && EndsWith(to, "_Hold")) {
    return 0.0f; // 溜め終わりと維持の開始は同一姿勢（ゼロブレンド）
  }
  if (EndsWith(from, "_Hold") && EndsWith(to, "_Hit")) {
    return 0.08f; // 維持→打つは80msクロスフェード、Hitの時計は即開始
  }
  return 0.2f;
}

} // namespace

void ClubController::AdvanceClip(const std::string &clipName, bool loop,
                                 float dt) {
  if (clipName != m_currentClipName) {
    m_prevClipName = m_currentClipName;
    m_prevClipTime = m_clipTime;
    m_crossfadeDuration = CrossfadeSeconds(m_currentClipName, clipName);
    m_crossfadeTimer = 0.0f;
    m_currentClipName = clipName;
    m_clipTime = 0.0f;
  }

  m_clipTime += dt;
  const float duration = ClipDuration(clipName);
  if (duration > 0.0f) {
    m_clipTime = loop ? std::fmod(m_clipTime, duration)
                      : std::clamp(m_clipTime, 0.0f, duration);
  }

  if (m_crossfadeTimer < m_crossfadeDuration) {
    m_crossfadeTimer += dt;
  }
}

void ClubController::UpdateGolferPose(core::GameContext &ctx) {
  if (!m_golferModelValid) {
    return;
  }

  PROFILE_SCOPE("Golfer.Skinning");
  const graphics::AnimationClip *clip =
      m_golferModel.FindClip(m_currentClipName);
  if (m_crossfadeDuration > 0.0f && m_crossfadeTimer < m_crossfadeDuration) {
    const graphics::AnimationClip *prev =
        m_golferModel.FindClip(m_prevClipName);
    const float prevTime =
        prev ? std::clamp(m_prevClipTime, 0.0f, prev->duration) : 0.0f;
    m_golferModel.ComputeBlendedPose(prev, prevTime, clip, m_clipTime,
                                     m_crossfadeTimer / m_crossfadeDuration,
                                     m_golferPoseScratch);
  } else {
    m_golferModel.ComputePose(clip, m_clipTime, m_golferPoseScratch);
  }

  if (auto *mesh = ctx.resource.GetMesh(m_golferMeshHandle)) {
    PROFILE_SCOPE("Golfer.Upload");
    mesh->UpdateVertices(ctx.graphics.GetContext(), m_golferPoseScratch);
  }
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

  // ゴルファーの構え位置候補: ボールから見て斜め後方、ショット方向を向いた位置。
  // ※ここでは「今のボール位置に対する理想の立ち位置」を計算するだけで、
  //   実際にその場へ移動するのはIdle中の歩行(下記)のみ。
  //   ショット実行中/結果表示中にボールが転がっても追従して瞬間移動しないようにする。
  const float facingYaw = std::atan2(shotDirection.x, shotDirection.z);
  const XMVECTOR localOffset =
      XMVectorSet(kGolferStanceOffsetX, 0.0f, kGolferStanceOffsetZ, 0.0f);
  const XMMATRIX rotMatrix = XMMatrixRotationY(facingYaw);
  const XMVECTOR worldOffset = XMVector3Transform(localOffset, rotMatrix);

  XMFLOAT3 targetStandPos;
  XMStoreFloat3(&targetStandPos,
               XMVectorAdd(XMLoadFloat3(&ballTr->position), worldOffset));
  // ゴルファーの足元はボールのY座標ではなく地面の高さに合わせる。
  // ボールがティーやスタートピンの上に乗って浮いている場合でも、
  // ゴルファーが宙に浮いて見えないようにするため。
  if (m_terrainSystem) {
    const float terrainHeight =
        m_terrainSystem->GetHeight(targetStandPos.x, targetStandPos.z);
    targetStandPos.y = game::physics::ToVisualSurfaceHeight(terrainHeight);
  } else {
    targetStandPos.y = ballTr->position.y;
  }

  if (!m_golferPositionInitialized) {
    // 初回は歩かずその場に配置する
    m_golferStandPos = targetStandPos;
    m_golferPositionInitialized = true;
  }

  const bool isIdlePhase = (shot->phase == ShotState::Phase::Idle);

  // Idle中にボールが新しい位置にあると判定したら歩いて向かう。
  // ただし新ホール開始などの大移動はカメラに映っていないため瞬間移動でよい。
  if (isIdlePhase && !m_golferWalking) {
    const float dx = targetStandPos.x - m_golferStandPos.x;
    const float dz = targetStandPos.z - m_golferStandPos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist > kGolferTeleportDistance) {
      m_golferStandPos = targetStandPos;
    } else if (dist > kGolferWalkTriggerDistance) {
      m_golferWalking = true;
      m_golferWalkTarget = targetStandPos;
    }
  }

  float yaw = facingYaw + XMConvertToRadians(kGolferYawOffsetDeg);
  std::string clipName = "Idle";
  bool loop = true;

  if (m_golferWalking) {
    const float dx = m_golferWalkTarget.x - m_golferStandPos.x;
    const float dz = m_golferWalkTarget.z - m_golferStandPos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.05f) {
      m_golferStandPos = m_golferWalkTarget;
      m_golferWalking = false;
    } else {
      const float step = (std::min)(kGolferWalkSpeed * dt, dist);
      m_golferStandPos.x += dx / dist * step;
      m_golferStandPos.z += dz / dist * step;
      m_golferStandPos.y = m_golferWalkTarget.y;
      yaw = std::atan2(dx, dz); // 歩く方向を向く
    }
    clipName = "WalkInPlace";
    loop = true;
  }

  clubTr->position = m_golferStandPos;
  XMStoreFloat4(&clubTr->rotation,
               XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f));

  clubMr->isVisible = true; // ゴルファーは常駐させ、非表示にはしない

  const bool swingInProgress = m_clubAnimPhase == ClubAnimPhase::Downswing ||
                               m_clubAnimPhase == ClubAnimPhase::FollowThrough;
  const bool swinging =
      shot->phase == ShotState::Phase::Executing ||
      (shot->phase == ShotState::Phase::ImpactTiming && shot->swingCommitted) ||
      ((shot->phase == ShotState::Phase::ShowResult ||
        shot->phase == ShotState::Phase::RestoringCamera) &&
       swingInProgress);

  if (swinging) {
    // 打つ・振り抜き(Hit)。インパクト確定の瞬間から再生し、クリップ開始から
    // GetImpactDelay()秒後(クラブ最下点)にシーン側がボールを発射する。
    clipName = GetPhaseClipName("Hit");
    loop = false;
    if (!swingInProgress && m_clubAnimPhase != ClubAnimPhase::Finished) {
      m_clubAnimPhase = ClubAnimPhase::Downswing;
      m_clubAnimTimer = 0.0f;
    }
    if (m_clubAnimPhase == ClubAnimPhase::Downswing &&
        m_currentClipName == clipName && m_clipTime >= ClipDuration(clipName)) {
      m_clubAnimPhase = ClubAnimPhase::FollowThrough;
    }
    if (m_clubAnimPhase == ClubAnimPhase::FollowThrough) {
      m_clubAnimTimer += dt;
      if (m_clubAnimTimer > 0.6f) {
        m_clubAnimPhase = ClubAnimPhase::Finished;
      }
    }
    if (m_clubAnimPhase == ClubAnimPhase::Finished) {
      // スイング後は構えていた場所でIdleへ戻る（ボールが止まったら歩いて追いかける）
      clipName = "Idle";
      loop = true;
    }
  } else {
    switch (shot->phase) {
    case ShotState::Phase::Idle:
      m_clubAnimPhase = ClubAnimPhase::Idle;
      m_clubAnimTimer = 0.0f;
      if (!m_golferWalking) {
        clipName = "Idle";
        loop = true;
      }
      break;

    case ShotState::Phase::PowerCharging:
    case ShotState::Phase::ImpactTiming: {
      // ため量(ゲージ値)に関わらず溜め(Charge)を最後まで再生し、維持(Hold)で待機する
      if (m_clubAnimPhase != ClubAnimPhase::Backswing) {
        m_clubAnimPhase = ClubAnimPhase::Backswing;
        m_chargeFinished = false;
      }
      const std::string chargeClip = GetPhaseClipName("Charge");
      if (!m_chargeFinished && m_currentClipName == chargeClip &&
          m_clipTime >= ClipDuration(chargeClip)) {
        m_chargeFinished = true;
      }
      clipName = m_chargeFinished ? GetPhaseClipName("Hold") : chargeClip;
      loop = m_chargeFinished;
      break;
    }

    default:
      clipName = "Idle";
      loop = true;
      break;
    }
  }

  AdvanceClip(clipName, loop, dt);
  UpdateGolferPose(ctx);
}

XMFLOAT3 ClubController::BeginCelebration(core::GameContext &ctx,
                                          const XMFLOAT3 &holePos,
                                          const XMFLOAT3 &cameraPos) {
  (void)ctx;
  const float h = GetGolferHeight();

  XMVECTOR toCam = XMVectorSet(cameraPos.x - holePos.x, 0.0f,
                               cameraPos.z - holePos.z, 0.0f);
  if (XMVectorGetX(XMVector3LengthSq(toCam)) < 1e-4f) {
    toCam = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
  }
  toCam = XMVector3Normalize(toCam);
  const XMVECTOR side =
      XMVectorSet(XMVectorGetZ(toCam), 0.0f, -XMVectorGetX(toCam), 0.0f);

  XMVECTOR spot = XMLoadFloat3(&holePos);
  spot = XMVectorAdd(spot, XMVectorScale(toCam, kCelebrateFrontRatio * h));
  spot = XMVectorAdd(spot, XMVectorScale(side, kCelebrateSideRatio * h));
  XMStoreFloat3(&m_celebrationSpot, spot);
  m_celebrationSpot.y = holePos.y;

  const float dx = m_celebrationSpot.x - m_golferStandPos.x;
  const float dz = m_celebrationSpot.z - m_golferStandPos.z;
  if (!m_golferPositionInitialized ||
      std::sqrt(dx * dx + dz * dz) > kCelebrateMaxWalkRatio * h) {
    // 遠くにいる場合は、画面外(ポールの横方向)から歩いて入ってくる位置へ瞬間移動する
    XMStoreFloat3(&m_golferStandPos,
                  XMVectorAdd(spot, XMVectorScale(side, kCelebrateWalkInRatio * h)));
    m_golferStandPos.y = holePos.y;
    m_golferPositionInitialized = true;
  }

  m_golferWalking = false;
  m_clubAnimPhase = ClubAnimPhase::Idle;
  m_clubAnimTimer = 0.0f;
  m_celebrationStage = CelebrationStage::Walking;
  return m_celebrationSpot;
}

void ClubController::UpdateCelebration(core::GameContext &ctx, float dt,
                                       const XMFLOAT3 &cameraPos) {
  if (m_celebrationStage == CelebrationStage::None) {
    return;
  }

  auto *tr = ctx.world.Get<Transform>(m_clubModelEntity);
  auto *mr = ctx.world.Get<MeshRenderer>(m_clubModelEntity);
  if (!tr || !mr || !m_golferModelValid) {
    m_celebrationStage = CelebrationStage::Done;
    return;
  }
  mr->isVisible = true;

  std::string clipName = "Celebrate";
  bool loop = false;
  // 到着後はカメラの方を向く（歩行時と同じく+Zを正面とする向き）
  float yaw = std::atan2(cameraPos.x - m_golferStandPos.x,
                         cameraPos.z - m_golferStandPos.z);

  if (m_celebrationStage == CelebrationStage::Walking) {
    const float dx = m_celebrationSpot.x - m_golferStandPos.x;
    const float dz = m_celebrationSpot.z - m_golferStandPos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.05f) {
      m_golferStandPos = m_celebrationSpot;
      m_celebrationStage = CelebrationStage::Celebrating;
    } else {
      const float step = (std::min)(kGolferWalkSpeed * dt, dist);
      m_golferStandPos.x += dx / dist * step;
      m_golferStandPos.z += dz / dist * step;
      m_golferStandPos.y = m_celebrationSpot.y;
      yaw = std::atan2(dx, dz);
      clipName = "WalkInPlace";
      loop = true;
    }
  }

  if (m_celebrationStage == CelebrationStage::Celebrating &&
      m_currentClipName == clipName && m_clipTime >= ClipDuration(clipName)) {
    m_clubAnimTimer += dt;
    if (m_clubAnimTimer > kCelebrateHoldSeconds) {
      m_celebrationStage = CelebrationStage::Done;
    }
  }

  tr->position = m_golferStandPos;
  XMStoreFloat4(&tr->rotation,
                XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f));

  AdvanceClip(clipName, loop, dt);
  UpdateGolferPose(ctx);
}

void ClubController::EndCelebration() {
  m_celebrationStage = CelebrationStage::None;
  m_golferPositionInitialized = false; // 次のコースではボール横に取り直す
  m_golferWalking = false;
  m_clubAnimPhase = ClubAnimPhase::Idle;
  m_clubAnimTimer = 0.0f;
}

const ClubController::Club &ClubController::GetCurrentClub() const {
  return m_currentClub;
}

int ClubController::GetCurrentClubIndex() const { return m_currentClubIndex; }

float ClubController::GetRecommendedCameraDistance(float fieldScale) const {
  if (m_currentClub.categoryEN == "Putter") {
    return 4.0f * fieldScale;
  }
  if (m_currentClub.categoryEN == "Wedge") {
    return 10.0f * fieldScale;
  }
  return 15.0f * fieldScale;
}

float ClubController::GetRecommendedCameraHeight(float fieldScale) const {
  if (m_currentClub.categoryEN == "Putter") {
    return 8.0f * fieldScale;
  }
  if (m_currentClub.categoryEN == "Wedge") {
    return 6.0f * fieldScale;
  }
  return 5.0f * fieldScale;
}

void ClubController::InitializeClubs(core::GameContext &ctx) {
  m_availableClubs.clear();

  m_availableClubs = {
      {"ドライバー", 119.0f, 10.5f, "Assets/textures/Clubs/1W_driver.png", 1.0f, "1W", "Driver"},
      {"3番ウッド", 92.0f, 15.0f, "Assets/textures/Clubs/3W_fairway_wood.png", 1.0f, "3W", "Wood"},
      {"5番ウッド", 79.0f, 18.0f, "Assets/textures/Clubs/5W_fairway_wood.png", 1.0f, "5W", "Wood"},
      {"4番ユーティリティ", 68.0f, 22.0f, "Assets/textures/Clubs/4U_utility.png", 1.0f, "4U", "Utility"},
      {"5番アイアン", 66.0f, 25.0f, "Assets/textures/Clubs/5I_iron.png", 1.0f, "5I", "Iron"},
      {"6番アイアン", 61.0f, 28.0f, "Assets/textures/Clubs/6I_iron.png", 1.0f, "6I", "Iron"},
      {"7番アイアン", 55.0f, 31.0f, "Assets/textures/Clubs/7I_iron.png", 1.0f, "7I", "Iron"},
      {"8番アイアン", 49.0f, 35.0f, "Assets/textures/Clubs/8I_iron.png", 1.0f, "8I", "Iron"},
      {"9番アイアン", 46.0f, 40.0f, "Assets/textures/Clubs/9I_iron.png", 1.0f, "9I", "Iron"},
      {"ピッチングウェッジ", 41.0f, 45.0f, "Assets/textures/Clubs/PW_pitching_wedge.png", 1.0f, "PW", "Wedge"},
      {"アプローチウェッジ", 37.0f, 50.0f, "Assets/textures/Clubs/AW_approach_wedge.png", 1.0f, "AW", "Wedge"},
      {"サンドウェッジ", 32.0f, 56.0f, "Assets/textures/Clubs/SW_sand_wedge.png", 1.0f, "SW", "Wedge"},
      {"ロブウェッジ", 27.0f, 60.0f, "Assets/textures/Clubs/LW_lob_wedge.png", 1.0f, "LW", "Wedge"},
      {"パター", 10.0f, 0.0f, "Assets/textures/Clubs/PT_putter.png", 1.0f, "PT", "Putter"},
  };

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

}

void ClubController::InitializeClubModel(core::GameContext &ctx) {
  m_clubModelEntity = m_entityOwner.Create(ctx.world);

  auto &tr = ctx.world.Add<Transform>(m_clubModelEntity);
  tr.position = {0.0f, 0.0f, 0.0f};
  tr.scale = {1.0f, 1.0f, 1.0f};

  auto &mr = ctx.world.Add<MeshRenderer>(m_clubModelEntity);
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {0.9f, 0.9f, 0.9f, 1.0f};
  mr.isVisible = false;

  m_golferModelValid =
      m_golferModel.LoadFromFile("Assets/models/G01_Robot_Golfer_Phases.glb");

  if (m_golferModelValid) {
    tr.scale = {kGolferScale, kGolferScale, kGolferScale};

    m_golferPoseScratch = m_golferModel.GetBindPoseVertices();
    m_golferMeshHandle = ctx.resource.CreateDynamicSkinnedMesh(
        m_golferPoseScratch, m_golferModel.GetIndices());
    mr.mesh = m_golferMeshHandle;

    // パーツごとのbaseColorFactorを頂点カラーとして焼き込んで塗り分けている
    // （SkeletalModel::LoadFromFile参照）。このモデルの埋め込みテクスチャは
    // 透明背景+デカール線のみの装飾用画像で、機体全体のベースカラーとして
    // 適用するとアルファテストでデカール以外が破棄されワイヤーフレーム状に
    // なってしまうため使用しない（1メッシュ=1テクスチャの制約のため、
    // パーツ単色とデカールの部分適用を両立できない）。
    mr.hasTexture = false;
    mr.color = {1.0f, 1.0f, 1.0f, 1.0f};

    LOG_INFO("WikiGolf", "Robot golfer model loaded (scale={})",
             kGolferScale);
  } else {
    LOG_ERROR("WikiGolf", "Robot golfer model load failed. Falling back to "
                          "the static club model");
    mr.mesh = ctx.resource.LoadMesh("Assets/models/golf_club.glb");
    tr.scale = {0.5f, 0.5f, 0.5f};
  }

  m_clubAnimPhase = ClubAnimPhase::Idle;
  m_clubAnimTimer = 0.0f;
  m_currentClipName = "Idle";
  m_clipTime = 0.0f;

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

  LOG_INFO("WikiGolf", "Switched Club: {}", m_currentClub.name);
  return true;
}

bool ClubController::SelectClubForAimPin(
    core::GameContext &ctx, const std::vector<float> &requiredPowerRatios,
    bool pathHasAbnormalSlope) {
  if (m_availableClubs.empty() ||
      requiredPowerRatios.size() != m_availableClubs.size()) {
    return false;
  }

  std::vector<game::utils::AimPinClubCandidate> candidates;
  candidates.reserve(m_availableClubs.size());
  for (size_t i = 0; i < m_availableClubs.size(); ++i) {
    const auto &club = m_availableClubs[i];
    candidates.push_back({requiredPowerRatios[i], club.baseCarryDistance,
                          club.categoryEN == "Putter"});
  }

  const size_t bestIndex =
      game::utils::SelectAimPinClubIndex(candidates, pathHasAbnormalSlope);
  return SelectClubByIndex(ctx, bestIndex);
}

bool ClubController::ResetToFirstClub(core::GameContext &ctx) {
  return SelectClubByIndex(ctx, 0);
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

  LOG_INFO("WikiGolf", "Switched to club: {}", m_currentClub.name);
  return true;
}

} // namespace game::controllers
