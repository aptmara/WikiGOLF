/**
 * @file PhysicsSystemSimulation.cpp
 * @brief 物理サブステップの実行
*/

#include "PhysicsSystemInternals.h"
#include "AchievementEvent.h"
#include "AchievementEventBus.h"
#include "CollisionDebugInfo.h"
#include "GameJuiceSystem.h"
#include "../../audio/AudioSystem.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "PhysicsFriction.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

void SimulatePhysicsSubsteps(PhysicsUpdateContext &frame) {
  auto &ctx = frame.gameContext;
  const float subDt = frame.subDt;
  const int subSteps = frame.subSteps;
  const XMVECTOR gravity = frame.gravity;
  TerrainData *terrainData = frame.terrainData;
  const ecs::Entity terrainEntity = frame.terrainEntity;
  GolfGameState *golfState = frame.golfState;
  const ecs::Entity ballEntity = frame.ballEntity;
  const HoleSpatialGrid &holeGrid = frame.holeGrid;
  const float maxHoleQueryRange = frame.maxHoleQueryRange;
  auto &dynamicBodies = frame.dynamicBodies;
  auto &perfStats = frame.perfStats;
  auto &jitterCursor = frame.jitterCursor;
  auto &rollingAudioTimer = frame.rollingAudioTimer;
  auto &holeSlowMotionCooldown = frame.holeSlowMotionCooldown;

  // サブステップループ
  for (int step = 0; step < subSteps; ++step) {
    // 動的オブジェクトの更新
    for (auto &body : dynamicBodies) {
      Transform &t = *body.t;
      RigidBody &rb = *body.rb;
      Collider &col = *body.c;

      XMVECTOR pos = XMLoadFloat3(&t.position);
      XMVECTOR vel = XMLoadFloat3(&rb.velocity);

      float posX = XMVectorGetX(pos);
      float posY = XMVectorGetY(pos);
      float posZ = XMVectorGetZ(pos);

      // マテリアル・地形高さ・法線をまとめて取得します。
      TerrainSample terrainSample;
      if (terrainData) {
        terrainSample = SampleTerrainAt(*terrainData, posX, posZ);
        ++perfStats.terrainSamples;
      }
      uint8_t mat = terrainSample.material; // フェアウェイ（デフォルト値）

      // 水・溶岩などOB地形に静止したらOBフラグを立てる（通過時はセーフ）
      const TerrainMaterial currentMaterial = static_cast<TerrainMaterial>(mat);
      if (IsOutOfBoundsTerrainMaterial(currentMaterial)) {
        if (golfState && body.entity == ballEntity) {
          float speed = SafeLength(vel);
          // 速度が十分低い(静止状態)ときのみOB
          if (speed < 0.5f) {
            golfState->isOB = true;
            LOG_INFO("Physics", "Ball stopped in OB terrain. material={}",
                     static_cast<int>(currentMaterial));
            if (ctx.achievementEvents) {
              AchievementEvent hazardEntered;
              hazardEntered.type = AchievementEventType::HazardEntered;
              hazardEntered.material = currentMaterial;
              ctx.achievementEvents->Publish(hazardEntered);
            }
          }
        }
      }

      // NaNチェック - 異常値なら位置リセット
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "NaN detected, resetting position");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
        XMStoreFloat3(&t.position, pos);
        XMStoreFloat3(&rb.velocity, vel);
        continue;
      }

      // 速度クランプ（ドライバー飛距離3倍化に伴い上限を拡張。）
      float speed = SafeLength(vel);
      if (speed > 300.0f) {
        vel = XMVectorScale(SafeNormalize(vel), 300.0f);
      }

      // 加速度計算
      XMVECTOR acc = gravity;

      // 地形衝突判定
      bool isGrounded = false;
      XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);

      if (terrainData && col.type == ColliderType::Sphere) {
        float terrainH = 0.0f;
        XMVECTOR terrainN;

        bool insideHole = false;
        float carveDepth = 0.0f;
        holeGrid.Query(posX, posZ, 0.5f, [&](const HoleInfo &hole) {
          ++perfStats.holeCandidates;
          float dx = posX - XMVectorGetX(hole.position);
          float dz = posZ - XMVectorGetZ(hole.position);
          float distSq = dx * dx + dz * dz;
          // ホール視覚サイズに合わせた判定（scale 0.5 = 半径0.5）
          float holeVisualRadius = 0.5f; // ビジュアルと統一
          if (distSq < holeVisualRadius * holeVisualRadius &&
              std::abs(posY - XMVectorGetY(hole.position)) < 2.0f) {
            insideHole = true;
            carveDepth = 0.6f; // 穴の深さ

            // ホール内からの脱出防止：縁に向かう速度をカット
            float dist = std::sqrt(distSq);
            if (dist > 0.01f) {
              // ボールからホール中心への方向
              XMVECTOR toCenter = XMVectorSubtract(hole.position, pos);
              toCenter = XMVectorSetY(toCenter, 0.0f); // XZ平面のみ
              toCenter = XMVector3Normalize(toCenter);

              // 現在の速度のうち、縁に向かう成分（中心から離れる方向）
              float velOutward = -XMVectorGetX(XMVector3Dot(vel, toCenter));
              if (velOutward > 0.0f) {
                // 縁に向かう速度を大幅カット（脱出防止）
                vel = XMVectorAdd(vel,
                                  XMVectorScale(toCenter, velOutward * 0.9f));
              }
            }
          }
        });

        if (terrainSample.valid) {
          terrainH = game::physics::ToVisualSurfaceHeight(terrainSample.height);
          const float visualSurfaceHeight = terrainH;
          terrainN = terrainSample.normal;
          if (insideHole) {
            terrainH -= carveDepth;
            terrainN = XMVectorSet(0, 1, 0, 0);
          } else {
            const float verticalImpactSpeed =
                std::max(0.0f, -XMVectorGetY(vel));
            terrainH -= ComputeSurfaceSinkDepth(
                static_cast<TerrainMaterial>(mat), verticalImpactSpeed,
                SafeLength(vel), col.radius);
          }

          float ballBottom = posY - col.radius;
          float penetration = terrainH - ballBottom;

          if (penetration > 0.0f) {
            if (insideHole) {
              penetration = std::min(penetration, 0.01f);
            }
            if (step == subSteps - 1 &&
                terrainEntity != ecs::NULL_ENTITY) {
              XMFLOAT3 centerValue;
              XMFLOAT3 normalValue;
              XMStoreFloat3(&centerValue, pos);
              XMStoreFloat3(&normalValue, terrainN);
              frame.events.events.push_back(MakeSphereCollisionEvent(
                  body.entity, terrainEntity, centerValue, col.radius,
                  normalValue, penetration));
            }
            // めり込み解消（法線方向に押し出し）
            float ny = std::max(XMVectorGetY(terrainN), 0.1f);
            float pushAmount = penetration / ny;
            pushAmount =
                std::min(pushAmount, col.radius * 2.0f); // 過度な押し出し防止

            pos = XMVectorAdd(pos, XMVectorScale(terrainN, pushAmount));

            // 速度の法線成分を処理
            float vn = XMVectorGetX(XMVector3Dot(vel, terrainN));
            if (vn < 0.0f) {
              const TerrainMaterial surfaceMaterial =
                  static_cast<TerrainMaterial>(mat);
              const float impactSpeed = std::abs(vn);
              const float incomingSpeed = SafeLength(vel);
              // 衝突による反射ベクトルを計算し、僅かなランダム挙動を加算
              float jitter = GetJitterFromTable(jitterCursor, 0.18f);
              float bounce = std::max(0.0f, rb.restitution * 0.5f * jitter);
              if (surfaceMaterial == TerrainMaterial::Bunker) {
                bounce = 0.0f;
              }
              vel = XMVectorSubtract(
                  vel, XMVectorScale(terrainN, vn * (1.0f + bounce)));

              if (surfaceMaterial == TerrainMaterial::Bunker) {
                const float retention =
                    ComputeSurfaceImpactTangentialRetention(
                        surfaceMaterial, impactSpeed, incomingSpeed);
                const float remainingNormalSpeed =
                    XMVectorGetX(XMVector3Dot(vel, terrainN));
                const XMVECTOR normalVelocity =
                    XMVectorScale(terrainN, remainingNormalSpeed);
                const XMVECTOR tangentialVelocity =
                    XMVectorSubtract(vel, normalVelocity);
                vel = XMVectorAdd(
                    normalVelocity,
                    XMVectorScale(tangentialVelocity, retention));
              }

              // 一定の速度以上で衝突した際にバウンド演出および効果音を再生
              if (impactSpeed > 2.0f) {
                float strength =
                    std::clamp((impactSpeed - 2.0f) / 10.0f, 0.0f, 1.0f);

                // マテリアルエフェクト
                auto **juiceSlot =
                    ctx.world.GetGlobal<GameJuiceSystem *>();
                if (juiceSlot && *juiceSlot) {
                  XMFLOAT3 impactPosition = {XMVectorGetX(pos),
                                             visualSurfaceHeight,
                                             XMVectorGetZ(pos)};
                  (*juiceSlot)->TriggerMaterialEffect(
                      ctx, impactPosition, static_cast<TerrainMaterial>(mat),
                      strength);
                  if (impactSpeed > 3.0f &&
                      static_cast<TerrainMaterial>(mat) !=
                          TerrainMaterial::Bunker) {
                    float rippleRadius = col.radius * 8.0f;
                    (*juiceSlot)->TriggerRippleEffect(
                        ctx, impactPosition, rippleRadius, strength);
                  }
                }

                // SE再生
                std::string seName = "se_shot_soft";
                float volume = strength;
                float pitch = 1.0f;

                switch (static_cast<TerrainMaterial>(mat)) {
                case TerrainMaterial::Bunker:
                  seName = "se_Bunker";
                  break;
                case TerrainMaterial::Rough:
                  seName = "se_Rough";
                  volume *= 0.8f;
                  break;
                case TerrainMaterial::Green:
                  seName = "se_Fairway";
                  pitch = 1.1f; // グリーンは硬め
                  break;
                default: // Fairway
                  seName = "se_Fairway";
                  break;
                }

                if (ctx.audio) {
                  ctx.audio->PlaySE(ctx, seName, volume, pitch);
                }
                // なければ volumeのみ。
              }
            }

            isGrounded = true;
            groundNormal = terrainN;
          } else if (penetration >
                     -std::max(0.002f, col.radius * 0.15f)) {
            // 接地マージン
            isGrounded = true;
            groundNormal = terrainN;
          }
        }
      } else {
        // フォールバック: 平面コリジョン (y=0)
        float posY = XMVectorGetY(pos);
        float bottom = posY - col.radius;

        if (bottom < 0.0f) {
          pos = XMVectorSetY(pos, col.radius);
          float vy = XMVectorGetY(vel);
          if (vy < 0.0f) {
            vel = XMVectorSetY(vel, -vy * rb.restitution);
          }
          isGrounded = true;
        }
      }

      // ホール吸引（ボールのみ対象）
      if (body.entity == ballEntity && col.type == ColliderType::Sphere) {
        float ballY = XMVectorGetY(pos);
        holeGrid.Query(posX, posZ, maxHoleQueryRange,
                       [&](const HoleInfo &hole) {
          ++perfStats.holeCandidates;
          float holeY = XMVectorGetY(hole.position);
          if (std::abs(ballY - holeY) > 1.0f)
            return;

          XMVECTOR toHole = XMVectorSubtract(hole.position, pos);
          float distSq = XMVectorGetX(
              XMVector3LengthSq(XMVectorSetY(toHole, 0.0f))); // XZ距離
          float range = hole.suctionRange;

          if (distSq < range * range && distSq > 0.001f) {
            float verticalBias =
                XMVectorGetY(toHole) - 0.05f; // 常にわずかに下向きに引く
            verticalBias = std::min(verticalBias, -0.05f);
            XMVECTOR pullVec = XMVectorSetY(toHole, verticalBias);

            float dirLen = SafeLength(pullVec);
            XMVECTOR dir = XMVectorZero();
            if (dirLen > 0.0001f) {
              dir = XMVectorScale(pullVec, 1.0f / dirLen);
            }
            float dist = std::sqrt(distSq);
            float normalized = std::clamp(dist / range, 0.0f, 1.0f);
            float expo = std::exp(-normalized * normalized * 4.5f);
            float ease = std::pow(std::max(0.0f, 1.0f - normalized), 2.2f);
            float factor =
                std::clamp((expo * 0.65f + ease * 0.75f), 0.0f, 1.1f);
            if (holeSlowMotionCooldown <= 0.0f) {
              auto **juiceSlot =
                  ctx.world.GetGlobal<GameJuiceSystem *>();
              if (juiceSlot && *juiceSlot) {
                float slowScale = 0.35f + normalized * 0.25f;
                float slowDuration = 0.5f + (1.0f - normalized) * 0.4f;
                (*juiceSlot)->TriggerSlowMotion(slowDuration, slowScale);
                holeSlowMotionCooldown = 0.25f;
              }
            }
            acc = XMVectorAdd(acc, XMVectorScale(dir, hole.gravity * factor));
          }
        });
      }

      // 接地時は法線方向の加速度を除去し、斜面方向の重力のみを残す
      if (isGrounded) {
        XMVECTOR normalComponent = XMVectorScale(
            groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
        acc = XMVectorSubtract(acc, normalComponent);
      }

      // 接地時の摩擦と斜面処理
      if (isGrounded) {
        // 接地時における斜面に沿った摩擦力と重力加速度の減衰処理
        float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
        if (vn < 0.0f) {
          vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
        }

        XMVECTOR slopeAccel = XMVectorSubtract(
            gravity,
            XMVectorScale(groundNormal,
                          XMVectorGetX(XMVector3Dot(gravity, groundNormal))));
        float slopeMag = SafeLength(slopeAccel);
        XMVECTOR slopeDir = XMVectorZero();
        if (slopeMag > 0.0001f) {
          slopeDir = XMVectorScale(slopeAccel, 1.0f / slopeMag);
        }

        // ゼロ速になっても斜面なら滑り出すための微小ブレークアウェイ
        float breakaway = 0.0f;
        if (slopeMag > 0.15f && SafeLength(vel) < 0.1f) {
          breakaway = 0.05f;
        }
        if (breakaway > 0.0f) {
          vel = XMVectorAdd(vel, XMVectorScale(slopeDir, breakaway * subDt));
        }

        float currentSpeed = SafeLength(vel);
        float terrainScale = 1.0f;
        if (terrainData) {
          terrainScale = terrainData->config.friction;
        }
        float ny = std::clamp(XMVectorGetY(groundNormal), 0.0f, 1.0f);
        float frictionAccel = ComputeGrassRollingAcceleration(
            currentSpeed, ny, static_cast<TerrainMaterial>(mat), terrainScale);
        if (golfState && body.entity == ballEntity) {
          float scale = golfState->rollingFrictionScale;
          if (!std::isfinite(scale) || scale < 0.05f) {
            scale = 1.0f;
          }
          frictionAccel *= scale;
        }

        // 接線方向の重力成分に対する静止摩擦チェック
        float tangentialAcc = SafeLength(acc);
        float staticLimit = frictionAccel * 1.2f;

        if (currentSpeed < 0.05f && tangentialAcc < staticLimit) {
          // ほぼ停止 & 重力に勝てる摩擦がある -> 完全停止
          vel = XMVectorZero();
          acc = XMVectorZero();
        } else if (currentSpeed > 0.0001f) {
          // 高速域の指数減衰と低速域の線形減衰をブレンドして自然な手触りで停止させる
          float t = std::clamp((currentSpeed - 1.0f) / 4.0f, 0.0f, 1.0f);

          float k = frictionAccel;
          float expRatio = std::exp(-k * subDt);

          float linearDrop = frictionAccel * subDt;
          float linearRatio = 0.0f;
          if (currentSpeed > linearDrop) {
            linearRatio = (currentSpeed - linearDrop) / currentSpeed;
          }
          float finalRatio = t * expRatio + (1.0f - t) * linearRatio;
          vel = XMVectorScale(vel, finalRatio);

          // 極低速時の停止判定
          if (SafeLength(vel) < 0.02f) {
            vel = XMVectorZero();
          }
        }

        // 極低速時の微細振動カット (Green上などでのピク付き防止)
        float speedAfter = SafeLength(vel);
        float slopeFlatness = XMVectorGetY(groundNormal);
        if (speedAfter < 0.03f && slopeFlatness > 0.90f) {
          vel = XMVectorZero();
        }
      }

      // 速度の二乗に比例する簡易的な空気抵抗をボールに対して常時適用
      speed = SafeLength(vel);
      if (speed > 0.001f) {
        float K = 0.000876f;
        float dragForce = K * rb.drag * speed * speed;
        float dragAccMagnitude = dragForce / rb.mass;

        XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
        XMVECTOR dragAcc = XMVectorScale(dragDir, dragAccMagnitude);

        acc = XMVectorAdd(acc, dragAcc);
      }

      // 風をボールにのみ適用する。TrajectoryPredictor::Predict の予測式と
      // 同じ計算式に揃え、狙い線と実際の飛球のズレを解消する。
      if (golfState && body.entity == ballEntity && golfState->windSpeed > 0.0f) {
        float windForce = golfState->windSpeed * 0.1f;
        XMVECTOR windVec = XMVectorSet(golfState->windDirection.x, 0,
                                        golfState->windDirection.y, 0);
        acc = XMVectorAdd(acc, XMVectorScale(windVec, windForce));
      }

      // オイラー積分 (復活)
      vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
      pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));
      // 最終NaNチェック
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "Post-integration NaN detected, resetting");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
      }

      // 停止判定（平坦時のみ）。
      float speedFinal = SafeLength(vel);
      float slopeFlatnessFinal = XMVectorGetY(groundNormal);
      if (speedFinal < 0.008f && isGrounded && slopeFlatnessFinal > 0.98f) {
        vel = XMVectorZero();
      }

      // 落下限界
      if (XMVectorGetY(pos) < -50.0f) {
        pos = XMVectorSet(0, 5, 0, 0);
        vel = XMVectorZero();
      }

      // 値を書き戻す
      XMStoreFloat3(&t.position, pos);
      XMStoreFloat3(&rb.velocity, vel);

      // ボールが実際に転がって見えるよう、水平方向の移動速度に応じて
      // 見た目の半径(kBallVisualScale基準)で転がり回転を積算する。
      // 当たり判定の半径(Collider::radius)は見た目より小さいため、
      // そちらを使うと接地点が滑っているように見えてしまう。
      if (body.entity == ballEntity) {
        XMVECTOR horizVel = XMVectorSetY(vel, 0.0f);
        float horizSpeed = XMVectorGetX(XMVector3Length(horizVel));
        if (horizSpeed > 0.001f) {
          const float renderRadius = game::physics::kBallVisualScale * 0.5f;
          XMVECTOR axis = XMVector3Normalize(XMVector3Cross(
              XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), horizVel));
          float angle = (horizSpeed * subDt) / renderRadius;
          XMVECTOR deltaRot = XMQuaternionRotationAxis(axis, angle);
          XMVECTOR curRot = XMLoadFloat4(&t.rotation);
          XMVECTOR newRot =
              XMQuaternionNormalize(XMQuaternionMultiply(curRot, deltaRot));
          XMStoreFloat4(&t.rotation, newRot);
        }
      }

      if (golfState && body.entity == ballEntity && step == subSteps - 1) {
        golfState->isBallGrounded = isGrounded;
        golfState->currentBallSpeed = speedFinal;
        if (isGrounded) {
          golfState->currentMaterial = static_cast<TerrainMaterial>(mat);
        }
      }

      UpdateRollingAudio(frame, body, isGrounded, speedFinal, mat, step);
    }

    if (step == subSteps - 1 && rollingAudioTimer >= (1.0f / 30.0f)) {
      rollingAudioTimer = 0.0f;
    }

    ResolveStaticCollisions(frame);

  }

}

} // namespace game::systems
