/**
 * @file PhysicsSystem.cpp
 * @brief 迚ｩ逅・ｼ皮ｮ励す繧ｹ繝・Β・亥ｮ牙ｮ夂沿・・
 *
 * 繧ｴ繝ｫ繝輔ご繝ｼ繝蜷代￠縺ｮ螳牙ｮ壹＠縺溽黄逅・す繝溘Η繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ繧呈署萓帙・
 * NaN髦ｲ豁｢縲∝慍蠖｢陦晉ｪ√√・繝ｼ繝ｫ蜷ｸ蠑輔ｒ螳溯｣・・
 */

#include "PhysicsSystem.h"
#include "../../audio/AudioSystem.h" // 蜉ｹ譫憺浹蜀咲函逕ｨ
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "GameJuiceSystem.h" // 貍泌・蜉ｹ譫懃畑
#include "PhysicsFriction.h"
#include "TerrainGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

// ========================================
// 螳牙・縺ｪ繝吶け繝医Ν貍皮ｮ励・繝ｫ繝代・
// ========================================

/**
 * @brief NaN繝√ぉ繝・け
 */
static bool IsNaN(float v) { return v != v; }

/**
 * @brief 繝吶け繝医Ν縺君aN繧貞性繧縺九メ繧ｧ繝・け
 */
static bool IsVectorNaN(XMVECTOR v) {
  float x = XMVectorGetX(v);
  float y = XMVectorGetY(v);
  float z = XMVectorGetZ(v);
  return IsNaN(x) || IsNaN(y) || IsNaN(z);
}

/**
 * @brief 螳牙・縺ｪ繝吶け繝医Ν豁｣隕丞喧・医ぞ繝ｭ繝吶け繝医Ν蟇ｾ遲厄ｼ・
 */
static XMVECTOR SafeNormalize(XMVECTOR v,
                              XMVECTOR fallback = XMVectorSet(0, 1, 0, 0)) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0001f) {
    return fallback;
  }
  return XMVector3Normalize(v);
}

/**
 * @brief 蛟､繧貞ｮ牙・縺ｪ遽・峇縺ｫ繧ｯ繝ｩ繝ｳ繝・
 */
static float SafeClamp(float v, float minVal, float maxVal) {
  if (IsNaN(v))
    return 0.0f;
  return std::clamp(v, minVal, maxVal);
}

/**
 * @brief 繝吶け繝医Ν縺ｮ髟ｷ縺輔ｒ螳牙・縺ｫ蜿門ｾ・
 */
static float SafeLength(XMVECTOR v) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0f || IsNaN(lenSq))
    return 0.0f;
  return std::sqrt(lenSq);
}

// ========================================
// 陦晉ｪ∝愛螳・
// ========================================

/**
 * @brief 逅・ｽ薙→OBB・域怏蜷大｢・阜繝懊ャ繧ｯ繧ｹ・峨・陦晉ｪ∝愛螳・
 */
static bool CheckSphereOBB(const XMFLOAT3 &spherePos, float radius,
                           const XMFLOAT3 &boxPos, const XMFLOAT3 &boxSize,
                           const XMFLOAT4 &boxRot, XMVECTOR &outNormal,
                           float &outDepth) {
  XMVECTOR sPos = XMLoadFloat3(&spherePos);
  XMVECTOR bPos = XMLoadFloat3(&boxPos);
  // 繝懊ャ繧ｯ繧ｹ縺ｮ繧ｵ繧､繧ｺ諠・ｱ縺九ｉ蜊翫し繧､繧ｺ・医ワ繝ｼ繝輔お繧ｯ繧ｹ繝・Φ繝茨ｼ峨ｒ邂怜・
  XMVECTOR bHalf = XMVectorScale(XMLoadFloat3(&boxSize), 0.5f);
  XMVECTOR bRot = XMLoadFloat4(&boxRot);

  // 逅・ｒ繝懊ャ繧ｯ繧ｹ縺ｮ繝ｭ繝ｼ繧ｫ繝ｫ蠎ｧ讓咏ｳｻ縺ｫ螟画鋤
  XMVECTOR relPos = XMVectorSubtract(sPos, bPos);
  XMVECTOR invRot = XMQuaternionInverse(bRot);
  XMVECTOR localPos = XMVector3Rotate(relPos, invRot);

  // 繝ｭ繝ｼ繧ｫ繝ｫ蠎ｧ讓咏ｳｻ縺ｧ縺ｮAABB蛻､螳夲ｼ医け繝ｩ繝ｳ繝暦ｼ・
  XMVECTOR closestLocal = XMVectorClamp(localPos, XMVectorNegate(bHalf), bHalf);

  // 霍晞屬繝√ぉ繝・け
  XMVECTOR distVecLocal = XMVectorSubtract(localPos, closestLocal);
  float d2 = XMVectorGetX(XMVector3LengthSq(distVecLocal));

  // 荳ｭ蠢・′螟門・縺ｫ縺ゅｋ蝣ｴ蜷・
  if (d2 > 0.00001f) {
    if (d2 > radius * radius) {
      return false; // 陦晉ｪ√↑縺・
    }

    float d = std::sqrt(d2);
    XMVECTOR localNormal = XMVectorScale(distVecLocal, 1.0f / d);
    outNormal = XMVector3Rotate(localNormal, bRot);
    outDepth = radius - d;
    return true;
  }

  // 荳ｭ蠢・′蜀・Κ縺ｫ縺ゅｋ蝣ｴ蜷茨ｼ壽怙繧りｿ代＞髱｢繧呈爾縺・
  float x = XMVectorGetX(localPos);
  float y = XMVectorGetY(localPos);
  float z = XMVectorGetZ(localPos);
  float hx = XMVectorGetX(bHalf);
  float hy = XMVectorGetY(bHalf);
  float hz = XMVectorGetZ(bHalf);

  // 蜷・擇縺ｸ縺ｮ霍晞屬・域ｭ｣: 蜀・・縺ｸ縺ｮ霍晞屬・・
  float dx_p = hx - x; // +X face
  float dx_n = x + hx; // -X face
  float dy_p = hy - y; // +Y face
  float dy_n = y + hy; // -Y face
  float dz_p = hz - z; // +Z face
  float dz_n = z + hz; // -Z face

  // 譛蟆上・邨ｶ蟇ｾ蛟､繧呈戟縺､霆ｸ繧呈爾縺呻ｼ医◎縺薙′譛繧よｵ・＞閼ｱ蜃ｺ繝ｫ繝ｼ繝茨ｼ・
  float minD = dx_p;
  int axis = 0; // 0:+x, 1:-x, 2:+y, 3:-y, 4:+z, 5:-z

  if (dx_n < minD) {
    minD = dx_n;
    axis = 1;
  }
  if (dy_p < minD) {
    minD = dy_p;
    axis = 2;
  }
  if (dy_n < minD) {
    minD = dy_n;
    axis = 3;
  }
  if (dz_p < minD) {
    minD = dz_p;
    axis = 4;
  }
  if (dz_n < minD) {
    minD = dz_n;
    axis = 5;
  }

  XMVECTOR localNormal;
  // 閼ｱ蜃ｺ譁ｹ蜷代・髱｢豕慕ｷ・
  switch (axis) {
  case 0:
    localNormal = XMVectorSet(1, 0, 0, 0);
    break;
  case 1:
    localNormal = XMVectorSet(-1, 0, 0, 0);
    break;
  case 2:
    localNormal = XMVectorSet(0, 1, 0, 0);
    break;
  case 3:
    localNormal = XMVectorSet(0, -1, 0, 0);
    break;
  case 4:
    localNormal = XMVectorSet(0, 0, 1, 0);
    break;
  case 5:
    localNormal = XMVectorSet(0, 0, -1, 0);
    break;
  }

  outNormal = XMVector3Rotate(localNormal, bRot);
  // 雋ｫ騾壽ｷｱ蠎ｦ = (陦ｨ髱｢縺ｾ縺ｧ縺ｮ霍晞屬) + 蜊雁ｾ・
  // minD縺ｯ縲瑚｡ｨ髱｢縺ｾ縺ｧ縺ｮ霍晞屬縲・
  outDepth = minD + radius;

  return true;
}

/**
 * @brief 蝨ｰ蠖｢縺ｮ鬮倥＆縺ｨ豕慕ｷ壹ｒ蜿門ｾ・
 */
static bool GetTerrainHeightAndNormal(const TerrainData &terrain, float x,
                                      float z, float &outHeight,
                                      XMVECTOR &outNormal) {
  float width = terrain.config.worldWidth;
  float depth = terrain.config.worldDepth;
  int resX = terrain.config.resolutionX;
  int resZ = terrain.config.resolutionZ;

  // UV蠎ｧ讓・(0.0~1.0)
  float u = (x / width) + 0.5f;
  float v = 0.5f - (z / depth);

  // 遽・峇螟悶メ繧ｧ繝・け
  if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) {
    outHeight = 0.0f;
    outNormal = XMVectorSet(0, 1, 0, 0);
    return false;
  }

  float fx = u * (resX - 1);
  float fz = v * (resZ - 1);

  int ix = static_cast<int>(fx);
  int iz = static_cast<int>(fz);

  // 蠅・阜繧ｯ繝ｩ繝ｳ繝・
  ix = std::clamp(ix, 0, resX - 2);
  iz = std::clamp(iz, 0, resZ - 2);

  float dx = fx - ix;
  float dz = fz - iz;

  // 螳牙・縺ｪ繧､繝ｳ繝・ャ繧ｯ繧ｹ繧｢繧ｯ繧ｻ繧ｹ
  auto GetHeightSafe = [&](int gx, int gz) -> float {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.heightMap.size())) {
      return terrain.heightMap[idx];
    }
    return 0.0f;
  };

  auto GetNormalSafe = [&](int gx, int gz) -> XMVECTOR {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.normals.size())) {
      XMVECTOR n = XMLoadFloat3(&terrain.normals[idx]);
      // 豕慕ｷ壹′荳肴ｭ｣縺ｪ蝣ｴ蜷医・繝・ヵ繧ｩ繝ｫ繝亥､
      if (IsVectorNaN(n)) {
        return XMVectorSet(0, 1, 0, 0);
      }
      return n;
    }
    return XMVectorSet(0, 1, 0, 0);
  };

  float h00 = GetHeightSafe(ix, iz);
  float h10 = GetHeightSafe(ix + 1, iz);
  float h01 = GetHeightSafe(ix, iz + 1);
  float h11 = GetHeightSafe(ix + 1, iz + 1);

  XMVECTOR n00 = GetNormalSafe(ix, iz);
  XMVECTOR n10 = GetNormalSafe(ix + 1, iz);
  XMVECTOR n01 = GetNormalSafe(ix, iz + 1);
  XMVECTOR n11 = GetNormalSafe(ix + 1, iz + 1);

  // 繝舌う繝ｪ繝九い陬憺俣
  float h0 = h00 * (1.0f - dx) + h10 * dx;
  float h1 = h01 * (1.0f - dx) + h11 * dx;
  outHeight = h0 * (1.0f - dz) + h1 * dz;

  // 豕慕ｷ壹・陬憺俣
  XMVECTOR n0 = XMVectorLerp(n00, n10, dx);
  XMVECTOR n1 = XMVectorLerp(n01, n11, dx);
  outNormal = SafeNormalize(XMVectorLerp(n0, n1, dz));

  // NaN繝√ぉ繝・け
  if (IsNaN(outHeight)) {
    outHeight = 0.0f;
  }

  return true;
}

// ========================================
// 繝｡繧､繝ｳ迚ｩ逅・す繧ｹ繝・Β
// ========================================

void PhysicsSystem(core::GameContext &ctx, float dt) {
  // DT繧ｭ繝｣繝・・・医Λ繧ｰ繧ｹ繝代う繧ｯ蟇ｾ遲厄ｼ・
  float clampedDt = std::min(dt, 0.033f); // 譛螟ｧ30FPS蛻・

  // 繧ｵ繝悶せ繝・ャ繝暦ｼ亥ｮ牙ｮ壽ｧ蜷台ｸ奇ｼ・
  const int subSteps = 4;
  float subDt = clampedDt / static_cast<float>(subSteps);

  // 驥榊鴨
  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);

  // 繧､繝吶Φ繝医Μ繧ｽ繝ｼ繧ｹ縺ｮ貅門ｙ
  auto *events = ctx.world.GetGlobal<CollisionEvents>();
  if (!events) {
    CollisionEvents newEvents;
    ctx.world.SetGlobal(std::move(newEvents));
    events = ctx.world.GetGlobal<CollisionEvents>();
  }
  events->events.clear();

  // 蝨ｰ蠖｢繝・・繧ｿ蜿門ｾ・
  TerrainData *terrainData = nullptr;
  ctx.world.Query<TerrainCollider>().Each(
      [&](ecs::Entity, TerrainCollider &tc) {
        if (tc.data) {
          terrainData = tc.data.get();
        }
      });

  // 繝帙・繝ｫ諠・ｱ蜿朱寔
  struct HoleInfo {
    XMVECTOR position;
    float radius;
    float gravity;
  };
  std::vector<HoleInfo> holes;
  ctx.world.Query<Transform, GolfHole>().Each(
      [&](ecs::Entity, Transform &t, GolfHole &h) {
        holes.push_back({XMLoadFloat3(&t.position), h.radius, h.gravity});
      });

  // 繧ｲ繝ｼ繝迥ｶ諷・
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();
  ecs::Entity ballEntity = 0xFFFFFFFF;
  if (golfState) {
    ballEntity = static_cast<ecs::Entity>(golfState->ballEntity);
  }

  // 繝・ヰ繝・げ繝ｭ繧ｰ逕ｨ
  static float debugTimer = 0.0f;
  debugTimer += clampedDt;

  // 繝輔Μ繝・ヱ繝ｼ蛻ｶ蠕｡・医ヴ繝ｳ繝懊・繝ｫ逕ｨ・・
  float flipperSpeed = 15.0f * clampedDt;
  ctx.world.Query<Transform, Flipper>().Each(
      [&](ecs::Entity, Transform &t, Flipper &f) {
        bool pressed = false;
        if (f.side == Flipper::Left && ctx.input.GetKey('Z'))
          pressed = true;
        if (f.side == Flipper::Right && ctx.input.GetKey(VK_OEM_2))
          pressed = true;

        float target = 0.0f;
        if (pressed) {
          target = 1.0f;
        }
        if (f.currentParam < target) {
          f.currentParam = std::min(f.currentParam + flipperSpeed, 1.0f);
        } else {
          f.currentParam = std::max(f.currentParam - flipperSpeed, 0.0f);
        }

        float angleDeg = f.currentParam * f.maxAngle;
        if (f.side == Flipper::Left)
          angleDeg *= -1.0f;

        XMVECTOR q = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0),
                                              XMConvertToRadians(angleDeg));
        XMStoreFloat4(&t.rotation, q);
      });

  // 繧ｵ繝悶せ繝・ャ繝励Ν繝ｼ繝・
  for (int step = 0; step < subSteps; ++step) {
    // 繝懊ョ繧｣繝ｪ繧ｹ繝亥庶髮・
    struct BodyInfo {
      ecs::Entity entity;
      Transform *t;
      RigidBody *rb;
      Collider *c;
    };
    std::vector<BodyInfo> dynamicBodies;
    std::vector<BodyInfo> staticBodies;

    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &c) {
          BodyInfo info = {e, &t, &rb, &c};
          if (!rb.isStatic) {
            dynamicBodies.push_back(info);
          }
          staticBodies.push_back(info);
        });

    // 蜍慕噪繧ｪ繝悶ず繧ｧ繧ｯ繝医・譖ｴ譁ｰ
    for (auto &body : dynamicBodies) {
      Transform &t = *body.t;
      RigidBody &rb = *body.rb;
      Collider &col = *body.c;

      XMVECTOR pos = XMLoadFloat3(&t.position);
      XMVECTOR vel = XMLoadFloat3(&rb.velocity);

      // 繝槭ユ繝ｪ繧｢繝ｫ蛻､螳夲ｼ医Ν繝ｼ繝怜・鬆ｭ縺ｧ螳滓命・・
      uint8_t mat = 0; // 繝輔ぉ繧｢繧ｦ繧ｧ繧､・医ョ繝輔か繝ｫ繝亥､・・
      if (terrainData) {
        float u = XMVectorGetX(pos) / terrainData->config.worldWidth + 0.5f;
        float v = 0.5f - XMVectorGetZ(pos) / terrainData->config.worldDepth;
        int ix = (int)(u * (terrainData->config.resolutionX - 1));
        int iz = (int)(v * (terrainData->config.resolutionZ - 1));
        if (ix >= 0 && ix < terrainData->config.resolutionX && iz >= 0 &&
            iz < terrainData->config.resolutionZ) {
          mat = terrainData
                    ->materialMap[iz * terrainData->config.resolutionX + ix];
        }
      }

      // Lava繧ｨ繝ｪ繧｢縺ｫ髱呎ｭ｢縺励◆繧碓B繝輔Λ繧ｰ繧堤ｫ九※繧具ｼ磯夐℃譎ゅ・繧ｻ繝ｼ繝包ｼ・
      if (static_cast<TerrainMaterial>(mat) == TerrainMaterial::Lava) {
        if (golfState && body.entity == ballEntity) {
          float speed = SafeLength(vel);
          // 騾溷ｺｦ縺悟香蛻・ｽ弱＞(髱呎ｭ｢迥ｶ諷・縺ｨ縺阪・縺ｿOB
          if (speed < 0.5f) {
            golfState->isOB = true;
            LOG_INFO("Physics", "Ball stopped in Lava zone - OB!");
          }
        }
      }

      // NaN繝√ぉ繝・け - 逡ｰ蟶ｸ蛟､縺ｪ繧我ｽ咲ｽｮ繝ｪ繧ｻ繝・ヨ
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "NaN detected, resetting position");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
        XMStoreFloat3(&t.position, pos);
        XMStoreFloat3(&rb.velocity, vel);
        continue;
      }

      // 騾溷ｺｦ繧ｯ繝ｩ繝ｳ繝・
      float speed = SafeLength(vel);
      if (speed > 100.0f) {
        vel = XMVectorScale(SafeNormalize(vel), 100.0f);
      }

      // 蜉騾溷ｺｦ險育ｮ・
      XMVECTOR acc = gravity;

      // 蝨ｰ蠖｢陦晉ｪ∝愛螳・
      bool isGrounded = false;
      XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);

      if (terrainData && col.type == ColliderType::Sphere) {
        float terrainH = 0.0f;
        XMVECTOR terrainN;

        float posX = XMVectorGetX(pos);
        float posY = XMVectorGetY(pos);
        float posZ = XMVectorGetZ(pos);

        bool insideHole = false;
        float carveDepth = 0.0f;
        XMVECTOR holeCenter = XMVectorZero();
        for (const auto &hole : holes) {
          float dx = posX - XMVectorGetX(hole.position);
          float dz = posZ - XMVectorGetZ(hole.position);
          float distSq = dx * dx + dz * dz;
          // 繝帙・繝ｫ隕冶ｦ壹し繧､繧ｺ縺ｫ蜷医ｏ縺帙◆蛻､螳夲ｼ・cale 0.5 = 蜊雁ｾ・.5・・
          float holeVisualRadius = 0.5f; // 繝薙ず繝･繧｢繝ｫ縺ｨ邨ｱ荳
          if (distSq < holeVisualRadius * holeVisualRadius &&
              std::abs(posY - XMVectorGetY(hole.position)) < 2.0f) {
            insideHole = true;
            carveDepth = 0.6f; // 遨ｴ縺ｮ豺ｱ縺・
            holeCenter = hole.position;

            // 繝帙・繝ｫ蜀・°繧峨・閼ｱ蜃ｺ髦ｲ豁｢・夂ｸ√↓蜷代°縺・溷ｺｦ繧偵き繝・ヨ
            float dist = std::sqrt(distSq);
            if (dist > 0.01f) {
              // 繝懊・繝ｫ縺九ｉ繝帙・繝ｫ荳ｭ蠢・∈縺ｮ譁ｹ蜷・
              XMVECTOR toCenter = XMVectorSubtract(hole.position, pos);
              toCenter = XMVectorSetY(toCenter, 0.0f); // XZ蟷ｳ髱｢縺ｮ縺ｿ
              toCenter = XMVector3Normalize(toCenter);

              // 迴ｾ蝨ｨ縺ｮ騾溷ｺｦ縺ｮ縺・■縲∫ｸ√↓蜷代°縺・・蛻・ｼ井ｸｭ蠢・°繧蛾屬繧後ｋ譁ｹ蜷托ｼ・
              float velOutward = -XMVectorGetX(XMVector3Dot(vel, toCenter));
              if (velOutward > 0.0f) {
                // 邵√↓蜷代°縺・溷ｺｦ繧貞､ｧ蟷・き繝・ヨ・郁┳蜃ｺ髦ｲ豁｢・・
                vel = XMVectorAdd(vel,
                                  XMVectorScale(toCenter, velOutward * 0.9f));
              }
            }
            break;
          }
        }

        if (GetTerrainHeightAndNormal(*terrainData, posX, posZ, terrainH,
                                      terrainN)) {
          if (insideHole) {
            terrainH -= carveDepth;
            terrainN = XMVectorSet(0, 1, 0, 0);
          }

          float ballBottom = posY - col.radius;
          float penetration = terrainH - ballBottom;

          if (penetration > 0.0f) {
            if (insideHole) {
              penetration = std::min(penetration, 0.01f);
            }
            // 繧√ｊ霎ｼ縺ｿ隗｣豸茨ｼ域ｳ慕ｷ壽婿蜷代↓謚ｼ縺怜・縺暦ｼ・
            float ny = std::max(XMVectorGetY(terrainN), 0.1f);
            float pushAmount = penetration / ny;
            pushAmount =
                std::min(pushAmount, col.radius * 2.0f); // 驕主ｺｦ縺ｪ謚ｼ縺怜・縺鈴亟豁｢

            pos = XMVectorAdd(pos, XMVectorScale(terrainN, pushAmount));

            // 騾溷ｺｦ縺ｮ豕慕ｷ壽・蛻・ｒ蜃ｦ逅・
            float vn = XMVectorGetX(XMVector3Dot(vel, terrainN));
            if (vn < 0.0f) {
              // 陦晉ｪ√↓繧医ｋ蜿榊ｰ・・繧ｯ繝医Ν繧定ｨ育ｮ励＠縲∝ュ縺九↑繝ｩ繝ｳ繝繝謖吝虚繧貞刈邂・
              float jitter = 1.0f + (((float)(rand() % 100) / 100.0f) - 0.5f) *
                                        0.18f;
              float bounce = std::max(0.0f, rb.restitution * 0.5f * jitter);
              vel = XMVectorSubtract(
                  vel, XMVectorScale(terrainN, vn * (1.0f + bounce)));

              // 荳螳壹・騾溷ｺｦ莉･荳翫〒陦晉ｪ√＠縺滄圀縺ｫ繝舌え繝ｳ繝画ｼ泌・縺翫ｈ縺ｳ蜉ｹ譫憺浹繧貞・逕・
              float impactSpeed = std::abs(vn);
              if (impactSpeed > 2.0f) {
                float strength =
                    std::clamp((impactSpeed - 2.0f) / 10.0f, 0.0f, 1.0f);

                // 繝槭ユ繝ｪ繧｢繝ｫ繧ｨ繝輔ぉ繧ｯ繝・
                auto *juice = ctx.world.GetGlobal<
                    GameJuiceSystem>();
                if (auto *juiceSys = ctx.world.GetGlobal<GameJuiceSystem>()) {
                  juiceSys->TriggerMaterialEffect(
                      ctx, t.position, static_cast<TerrainMaterial>(mat),
                      strength);
                  if (impactSpeed > 3.0f) {
                    float rippleRadius = col.radius * 8.0f;
                    juiceSys->TriggerRippleEffect(ctx, t.position, rippleRadius,
                                                  strength);
                  }
                }

                // SE蜀咲函
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
                  pitch = 1.1f; // 繧ｰ繝ｪ繝ｼ繝ｳ縺ｯ遑ｬ繧・
                  break;
                default: // Fairway
                  seName = "se_Fairway";
                  break;
                }

                if (ctx.audio) {
                  ctx.audio->PlaySE(ctx, seName, volume, pitch);
                }
                // 縺ｪ縺代ｌ縺ｰ volume縺ｮ縺ｿ縲・
              }
            }

            isGrounded = true;
            groundNormal = terrainN;
          } else if (penetration > -0.1f) {
            // 謗･蝨ｰ繝槭・繧ｸ繝ｳ
            isGrounded = true;
            groundNormal = terrainN;
          }
        }
      } else {
        // 繝輔か繝ｼ繝ｫ繝舌ャ繧ｯ: 蟷ｳ髱｢繧ｳ繝ｪ繧ｸ繝ｧ繝ｳ (y=0)
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

      // 繝帙・繝ｫ蜷ｸ蠑・
      if (col.type == ColliderType::Sphere) {
        float ballY = XMVectorGetY(pos);
        for (const auto &hole : holes) {
          float holeY = XMVectorGetY(hole.position);
          if (std::abs(ballY - holeY) > 1.0f)
            continue;

          XMVECTOR toHole = XMVectorSubtract(hole.position, pos);
          float distSq = XMVectorGetX(
              XMVector3LengthSq(XMVectorSetY(toHole, 0.0f))); // XZ霍晞屬
          float range = hole.radius * 2.5f;

          if (distSq < range * range && distSq > 0.001f) {
            float verticalBias =
                XMVectorGetY(toHole) - 0.05f; // 蟶ｸ縺ｫ繧上★縺九↓荳句髄縺阪↓蠑輔￥
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
            if (auto *juiceSys = ctx.world.GetGlobal<GameJuiceSystem>()) {
              float slowScale = 0.35f + normalized * 0.25f;
              float slowDuration = 0.5f + (1.0f - normalized) * 0.4f;
              juiceSys->TriggerSlowMotion(slowDuration, slowScale);
            }
            acc = XMVectorAdd(acc, XMVectorScale(dir, hole.gravity * factor));
          }
        }
      }

      // 謗･蝨ｰ譎ゅ・豕慕ｷ壽婿蜷代・蜉騾溷ｺｦ繧帝勁蜴ｻ縺励∵万髱｢譁ｹ蜷代・驥榊鴨縺ｮ縺ｿ繧呈ｮ九☆
      if (isGrounded) {
        XMVECTOR normalComponent = XMVectorScale(
            groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
        acc = XMVectorSubtract(acc, normalComponent);
      }

      // 謗･蝨ｰ譎ゅ・鞫ｩ謫ｦ縺ｨ譁憺擇蜃ｦ逅・
      if (isGrounded) {
        // 謗･蝨ｰ譎ゅ↓縺翫￠繧区万髱｢縺ｫ豐ｿ縺｣縺滓束謫ｦ蜉帙→驥榊鴨蜉騾溷ｺｦ縺ｮ貂幄｡ｰ蜃ｦ逅・
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

        // 繧ｼ繝ｭ騾溘↓縺ｪ縺｣縺ｦ繧よ万髱｢縺ｪ繧画ｻ代ｊ蜃ｺ縺吶◆繧√・蠕ｮ蟆上ヶ繝ｬ繝ｼ繧ｯ繧｢繧ｦ繧ｧ繧､
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

        // 迺ｰ蠅・憾諷九・菫晏ｭ假ｼ医・繝ｼ繝ｫ縺ｮ縺ｿ蟇ｾ雎｡・・
        if (golfState && body.entity == ballEntity) {
          golfState->isBallGrounded = true;
          golfState->currentMaterial = static_cast<TerrainMaterial>(mat);
          golfState->currentBallSpeed = currentSpeed;
        }

        // 謗･邱壽婿蜷代・驥榊鴨謌仙・縺ｫ蟇ｾ縺吶ｋ髱呎ｭ｢鞫ｩ謫ｦ繝√ぉ繝・け
        float tangentialAcc = SafeLength(acc);
        float staticLimit = frictionAccel * 1.2f;

        if (currentSpeed < 0.05f && tangentialAcc < staticLimit) {
          // 縺ｻ縺ｼ蛛懈ｭ｢ & 驥榊鴨縺ｫ蜍昴※繧区束謫ｦ縺後≠繧・-> 螳悟・蛛懈ｭ｢
          vel = XMVectorZero();
          acc = XMVectorZero();
        } else if (currentSpeed > 0.0001f) {
          // 鬮倬溷沺縺ｮ謖・焚貂幄｡ｰ縺ｨ菴朱溷沺縺ｮ邱壼ｽ｢貂幄｡ｰ繧偵ヶ繝ｬ繝ｳ繝峨＠縺ｦ閾ｪ辟ｶ縺ｪ謇玖ｧｦ繧翫〒蛛懈ｭ｢縺輔○繧・
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

          // 讌ｵ菴朱滓凾縺ｮ蛛懈ｭ｢蛻､螳・
          if (SafeLength(vel) < 0.02f) {
            vel = XMVectorZero();
          }
        }

        // 讌ｵ菴朱滓凾縺ｮ蠕ｮ邏ｰ謖ｯ蜍輔き繝・ヨ (Green荳翫↑縺ｩ縺ｧ縺ｮ繝斐け莉倥″髦ｲ豁｢)
        float speedAfter = SafeLength(vel);
        float slopeFlatness = XMVectorGetY(groundNormal);
        if (speedAfter < 0.03f && slopeFlatness > 0.90f) {
          vel = XMVectorZero();
        }
      }

      // 騾溷ｺｦ縺ｮ莠御ｹ励↓豈比ｾ九☆繧狗ｰ｡譏鍋噪縺ｪ遨ｺ豌玲慣謚励ｒ繝懊・繝ｫ縺ｫ蟇ｾ縺励※蟶ｸ譎る←逕ｨ
      speed = SafeLength(vel);
      if (speed > 0.001f) {
        float K = 0.000876f;
        float dragForce = K * rb.drag * speed * speed;
        float dragAccMagnitude = dragForce / rb.mass;

        XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
        XMVECTOR dragAcc = XMVectorScale(dragDir, dragAccMagnitude);

        acc = XMVectorAdd(acc, dragAcc);
      }

      // 繧ｪ繧､繝ｩ繝ｼ遨榊・ (蠕ｩ豢ｻ)
      vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
      pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));
      // 譛邨・aN繝√ぉ繝・け
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "Post-integration NaN detected, resetting");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
      }

      // 蛛懈ｭ｢蛻､螳夲ｼ亥ｹｳ蝮ｦ譎ゅ・縺ｿ・峨・
      float speedFinal = SafeLength(vel);
      float slopeFlatnessFinal = XMVectorGetY(groundNormal);
      if (speedFinal < 0.008f && isGrounded && slopeFlatnessFinal > 0.98f) {
        vel = XMVectorZero();
      }

      // 關ｽ荳矩剞逡・
      if (XMVectorGetY(pos) < -50.0f) {
        pos = XMVectorSet(0, 5, 0, 0);
        vel = XMVectorZero();
      }

      // 蛟､繧呈嶌縺肴綾縺・
      XMStoreFloat3(&t.position, pos);
      XMStoreFloat3(&rb.velocity, vel);

      // 謗･蝨ｰ荳ｭ縺ｮ襍ｰ陦碁浹 (Rolling SE)
      if (ctx.audio && body.entity == ballEntity) {
        if (isGrounded && speedFinal > 0.5f) {
          std::string seName = "se_Fairway";
          float pBase = 0.0f;
          switch (static_cast<TerrainMaterial>(mat)) {
          case TerrainMaterial::Bunker:
            seName = "se_Bunker";
            pBase = -0.2f;
            break;
          case TerrainMaterial::Rough:
            seName = "se_Rough";
            pBase = -0.1f;
            break;
          case TerrainMaterial::Green:
            seName = "se_Fairway";
            pBase = 0.1f;
            break;
          default:
            break;
          }
          // 騾溷ｺｦ縺ｫ蠢懊§縺ｦ髻ｳ驥上・繝斐ャ繝∬ｪｿ謨ｴ (0.0~1.0)
          float vol = std::clamp(speedFinal / 20.0f, 0.0f, 1.0f) * 0.4f;
          float p = pBase + (speedFinal / 40.0f);
          ctx.audio->SetLoopingSE(ctx, "BallRoll", seName, vol, p);
        } else {
          // 蛛懈ｭ｢荳ｭ縺ｾ縺溘・遨ｺ荳ｭ縺ｪ繧牙●豁｢
          ctx.audio->SetLoopingSE(ctx, "BallRoll", "", 0.0f);
        }
      }
    }

    // 髱咏噪繧ｪ繝悶ず繧ｧ繧ｯ繝医→縺ｮ陦晉ｪ・
    for (auto &dyn : dynamicBodies) {
      if (dyn.c->type != ColliderType::Sphere)
        continue;

      for (auto &other : staticBodies) {
        if (dyn.entity == other.entity)
          continue;
        if (other.c->type != ColliderType::Box)
          continue;

        XMVECTOR normal;
        float depth;
        XMFLOAT3 scaledSize = {other.c->size.x * other.t->scale.x,
                               other.c->size.y * other.t->scale.y,
                               other.c->size.z * other.t->scale.z};

        if (CheckSphereOBB(dyn.t->position, dyn.c->radius, other.t->position,
                           scaledSize, other.t->rotation, normal, depth)) {
          // 繝帙・繝ｫ縺ｯ繝医Μ繧ｬ繝ｼ縺ｮ縺ｿ
          if (ctx.world.Has<GolfHole>(other.entity)) {
            events->events.push_back({dyn.entity, other.entity});
            continue;
          }

          events->events.push_back({dyn.entity, other.entity});

          // 謚ｼ縺怜・縺・
          XMVECTOR pos = XMLoadFloat3(&dyn.t->position);
          pos = XMVectorAdd(pos, XMVectorScale(normal, depth));
          XMStoreFloat3(&dyn.t->position, pos);

          // 蜿榊ｰ・
          XMVECTOR vel = XMLoadFloat3(&dyn.rb->velocity);
          float vn = XMVectorGetX(XMVector3Dot(vel, normal));
          if (vn < 0.0f) {
            float jitter = 1.0f + (((float)(rand() % 100) / 100.0f) - 0.5f) *
                                      0.2f; // 霍ｳ縺ｭ譁ｹ縺ｫ蟆代＠繝ｩ繝ｳ繝繝縺輔ｒ莉倅ｸ・
            float bounce =
                std::max(0.0f, (dyn.rb->restitution + other.rb->restitution) *
                                   0.5f * jitter);
            vel = XMVectorSubtract(vel,
                                   XMVectorScale(normal, vn * (1.0f + bounce)));
            XMStoreFloat3(&dyn.rb->velocity, vel);
          }
        }
      }
    }
  }

  // 繝・ヰ繝・げ繝ｭ繧ｰ蜃ｺ蜉・
  if (debugTimer > 0.25f) {
    debugTimer = 0.0f;

    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &) {
          if (golfState && e == golfState->ballEntity) {
            XMVECTOR vel = XMLoadFloat3(&rb.velocity);
            float speed = SafeLength(vel);
            bool grounded = t.position.y < 1.0f; // 邁｡譏灘愛螳・
            std::string groundedStr = "N";
            if (grounded) groundedStr = "Y";
            LOG_DEBUG(
                "Physics",
                "ball speed={:.3f} grounded={} pos=({:.3f},{:.3f},{:.3f})",
                speed, groundedStr, t.position.x, t.position.y,
                t.position.z);
          }
        });
  }
}

} // namespace game::systems
