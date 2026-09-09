/**
 * @file WikiGolfSceneTrajectory.cpp
 * @brief 予測軌道と方向ガイドの更新処理を実装します。
*/

#include "WikiGolfScene.h"
#include "../../core/GameContext.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../controllers/ClubController.h"
#include "../controllers/TrajectoryPredictor.h"
#include "../systems/WikiTerrainSystem.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

void WikiGolfScene::UpdateTrajectoryAndGuide(
    core::GameContext &ctx, GolfGameState &state, ShotState &shot, float dt,
    bool tutorialInputLocked, bool isMapView) {
    // 予測軌道アップデート (Idle 時も表示: クラブ切り替えやパワーレビューのため)
    bool canShowTrajectory = m_trajectoryPredictor && state.canShoot &&
                             !tutorialInputLocked && !isMapView;
    if (canShowTrajectory &&
        (shot.phase == game::components::ShotState::Phase::Idle ||
         shot.phase == game::components::ShotState::Phase::PowerCharging ||
         shot.phase == game::components::ShotState::Phase::ImpactTiming)) {
        PROFILE_SCOPE("WikiGolf.Trajectory");
        game::controllers::TrajectoryPredictor::Params tParams;
        tParams.ballEntity    = m_ballEntity;
        tParams.arrowEntity   = m_arrowEntity;
        tParams.shotDirection = DirectX::XMFLOAT3(0, 0, 1);
        if (m_cameraController) {
            tParams.shotDirection = m_cameraController->GetShotDirection();
        }
        if (m_clubController) {
            const auto& currentClub = m_clubController->GetCurrentClub();
            tParams.baseCarryDistance = currentClub.baseCarryDistance;
            tParams.carryTable        = &currentClub.carryTable;
            tParams.launchAngle       = currentClub.launchAngle;
        } else {
            tParams.launchAngle = 30.0f;
        }
        tParams.isMapView     = false;
        tParams.terrainSystem = m_terrainSystem.get();

        float powerRatio = 0.0f; // Idle 時は 0 (TrajectoryPredictor 内でデフォルト比率を使用)
        if (shot.phase == game::components::ShotState::Phase::PowerCharging) {
            powerRatio = shot.powerGaugePos;
        } else if (shot.phase == game::components::ShotState::Phase::ImpactTiming) {
            // パワー決定後はゲージ値に関わらずクラブの基準飛距離(フルスイング相当)を
            // 固定表示する。実際の飛距離への反映はExecuteShot時の判定倍率で行う。
            powerRatio = 1.0f;
        } else if (shot.confirmedPower > 0.0f) {
            powerRatio = shot.confirmedPower;
        }

        tParams.powerRatio = std::clamp(powerRatio, 0.0f, 1.0f);
        m_trajectoryPredictor->Update(ctx, tParams);
    } else if (m_trajectoryPredictor) {
        m_trajectoryPredictor->Hide(ctx);
    }

    // 方向ガイド（流れる矢印アニメーション）
    {
      const bool showGuide = !m_guideSegments.empty() &&
                             (shot.phase == game::components::ShotState::Phase::Idle) &&
                             state.canShoot && !tutorialInputLocked && !isMapView;

      auto* ballT2 = ctx.world.Get<game::components::Transform>(m_ballEntity);
      if (showGuide && ballT2) {
        m_guideAnimTimer += dt;

        DirectX::XMFLOAT3 shotDir{0, 0, 1};
        if (m_cameraController) {
          shotDir = m_cameraController->GetShotDirection();
        }
        float yaw = std::atan2(shotDir.x, shotDir.z);
        DirectX::XMVECTOR qRot = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);

        // 各セグメントをボール前方に等間隔で並べ、色・サイズをグラデーション
        const int nSeg = static_cast<int>(m_guideSegments.size());
        const float kSpacing   = 1.05f; // セグメント間隔
        const float kScrollSpd = 2.0f;  // スクロール速度
        // アニメーションフェーズ: 0→1 で先頭に向かってスクロール
        float phase = std::fmod(m_guideAnimTimer * kScrollSpd, 1.0f);

        for (int si = 0; si < nSeg; ++si) {
          auto* segMR = ctx.world.Get<game::components::MeshRenderer>(m_guideSegments[si]);
          auto* segT  = ctx.world.Get<game::components::Transform>(m_guideSegments[si]);
          if (!segMR || !segT) continue;

          // 正規化比率：先端(si=0)＝0, 末尾＝1
          float nt = 0.0f;
          if (nSeg > 1) {
            nt = static_cast<float>(si) / static_cast<float>(nSeg - 1);
          }

          // スクロールオフセット: 各セグメントが時間経過で前方に流れる
          float scrolledNt = std::fmod(nt + phase, 1.0f);
          float dist = 0.6f + scrolledNt * (kSpacing * (float)(nSeg - 1));

          // 送り先位置: ボールせから前方 dist m
          DirectX::XMVECTOR fwd = DirectX::XMVectorSet(shotDir.x, 0, shotDir.z, 0);
          DirectX::XMVECTOR segPosV = DirectX::XMVectorAdd(
              DirectX::XMLoadFloat3(&ballT2->position),
              DirectX::XMVectorScale(fwd, dist));
          DirectX::XMFLOAT3 segPos;
          DirectX::XMStoreFloat3(&segPos, segPosV);
          // 地面に少し浮かせる
          if (m_terrainSystem) {
            segPos.y = m_terrainSystem->GetHeight(segPos.x, segPos.z) + 0.12f;
          } else {
            segPos.y = ballT2->position.y + 0.12f;
          }
          segT->position = segPos;
          DirectX::XMStoreFloat4(&segT->rotation, qRot);

          // 先端大く(1.6x)→末尾小さく(0.5x)
          float thick = 0.18f + (0.06f - 0.18f) * nt;
          // 先端のセグメントをヒシ形に見せる（scaleXを幅広に）
          float xScale = thick * (1.0f + (1.0f - nt) * 0.8f);
          segT->scale = {xScale, thick, 0.50f};

          // 色: 先端白/水色 → 末尾シアン/透明
          // scrolledNtが0に近いほど「新生」部分なので明るく、大きく表示
          float alpha = 0.85f - scrolledNt * 0.75f;
          float r = 0.50f + (1.00f - 0.50f) * (1.0f - scrolledNt);
          float g = 0.85f + (1.00f - 0.85f) * (1.0f - scrolledNt);
          float b = 1.00f;
          segMR->color = {r, g, b, alpha};
          segMR->isVisible = (alpha > 0.02f);
        }
      } else {
        // 非表示時は全セグメントを隠す
        for (auto segE : m_guideSegments) {
          if (auto* segMR = ctx.world.Get<game::components::MeshRenderer>(segE)) {
            segMR->isVisible = false;
          }
        }
      }
    }

    // 傾斜可視化（パター保持中、待機中のみグリーンの高低差オーバーレイを表示）
    {
      const bool isPutter = m_clubController &&
          m_clubController->GetCurrentClub().categoryEN == "Putter";
      const bool showSlope = isPutter && state.canShoot && !tutorialInputLocked &&
                             !isMapView &&
                             shot.phase == game::components::ShotState::Phase::Idle;

      DirectX::XMFLOAT3 ballPos{0.0f, 0.0f, 0.0f};
      if (auto* ballT3 = ctx.world.Get<game::components::Transform>(m_ballEntity)) {
        ballPos = ballT3->position;
      }
      m_slopeVisualization.Update(ctx, showSlope, ballPos, m_terrainSystem.get());
    }
}

} // namespace game::scenes
