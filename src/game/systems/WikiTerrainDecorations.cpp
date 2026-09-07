/**
 * @file WikiTerrainDecorations.cpp
 * @brief バイオーム別の地形装飾を実装します。
*/

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

/**
 * @brief バイオームに応じた装飾オブジェクトを生成します。
*/
void WikiTerrainSystem::CreateDecorations(core::GameContext &ctx,
                                          float fieldWidth, float fieldDepth,
                                          int biome) {
  if (!m_terrainData) {
    return;
  }

  int targetClusters = 15;
  if (biome == 1) {
    targetClusters = 12;
  } else if (biome == 2) {
    targetClusters = 10;
  } else if (biome == 3) {
    targetClusters = 16;
  }

  std::mt19937 rng(static_cast<unsigned>(biome * 12345 + 67890));
  std::uniform_real_distribution<float> distX(-fieldWidth * 0.4f,
                                               fieldWidth * 0.4f);
  std::uniform_real_distribution<float> distZ(-fieldDepth * 0.4f,
                                               fieldDepth * 0.4f);
  std::uniform_real_distribution<float> distScale(0.75f, 1.30f);
  std::uniform_real_distribution<float> distRot(0.0f, 6.28f);
  std::uniform_real_distribution<float> distOffset(-1.0f, 1.0f);

  const int resX = m_terrainData->config.resolutionX;
  const int resZ = m_terrainData->config.resolutionZ;
  const float sampleOffset =
      std::max(fieldWidth / static_cast<float>(resX - 1),
               fieldDepth / static_cast<float>(resZ - 1));

  auto materialAt = [&](float x, float z) -> uint8_t {
    float u = x / fieldWidth + 0.5f;
    float v = 0.5f - z / fieldDepth;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
      return 255;
    }
    int gx = std::clamp(
        static_cast<int>(u * static_cast<float>(resX - 1) + 0.5f), 0,
        resX - 1);
    int gz = std::clamp(
        static_cast<int>(v * static_cast<float>(resZ - 1) + 0.5f), 0,
        resZ - 1);
    return m_terrainData->materialMap[gz * resX + gx];
  };
  auto materialAllowed = [&](uint8_t material) {
    switch (biome) {
    case 0:
      return material == 1;
    case 1:
      return material == 2 || material == 7;
    case 2:
      return material == 4 || material == 7;
    case 3:
      return material == 1 || material == 7;
    default:
      return false;
    }
  };

  auto slopeAt = [&](float x, float z) {
    float hL = GetHeight(x - sampleOffset, z);
    float hR = GetHeight(x + sampleOffset, z);
    float hD = GetHeight(x, z - sampleOffset);
    float hU = GetHeight(x, z + sampleOffset);
    float dx = (hR - hL) / (sampleOffset * 2.0f);
    float dz = (hU - hD) / (sampleOffset * 2.0f);
    return std::sqrt(dx * dx + dz * dz);
  };

  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto cylinderMesh = ctx.resource.LoadMesh("builtin/cylinder_smooth");
  auto sphereMesh = ctx.resource.LoadMesh("builtin/sphere_smooth");
  auto rockMesh = ctx.resource.LoadMesh("builtin/rock");

  auto createPiece = [&](resources::MeshHandle mesh, const XMFLOAT4 &color,
                         const XMFLOAT3 &position, const XMFLOAT3 &scale,
                         float pitch, float yaw, float roll) {
    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = position;
    transform.scale = scale;
    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, quat);
    transform.rotation = rotF;

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = mesh;
    mr.shader = basicShader;
    mr.color = color;
    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);
  };

  std::vector<XMFLOAT2> clusterPositions;
  int createdClusters = 0;
  const int maxAttempts = targetClusters * 30;
  for (int attempt = 0;
       attempt < maxAttempts && createdClusters < targetClusters; ++attempt) {
    float x = distX(rng);
    float z = distZ(rng);
    if (!materialAllowed(materialAt(x, z))) {
      continue;
    }

    float slopeLimit = 0.85f;
    if (biome == 0) {
      slopeLimit = 0.42f;
    }
    if (slopeAt(x, z) > slopeLimit) {
      continue;
    }

    bool overlaps = false;
    for (const auto &position : clusterPositions) {
      float dx = x - position.x;
      float dz = z - position.y;
      if (dx * dx + dz * dz < 6.25f) {
        overlaps = true;
        break;
      }
    }
    if (overlaps) {
      continue;
    }

    clusterPositions.push_back({x, z});
    ++createdClusters;
    float terrainHeight = GetHeight(x, z);
    float scale = distScale(rng);
    float yaw = distRot(rng);

    if (biome == 0) {
      float trunkHeight = 1.8f * scale;
      createPiece(cylinderMesh, {0.30f, 0.20f, 0.11f, 1.0f},
                  {x, terrainHeight + trunkHeight * 0.5f, z},
                  {0.28f * scale, trunkHeight, 0.28f * scale}, 0.0f, yaw,
                  0.0f);
      const XMFLOAT4 leafColors[] = {
          {0.16f, 0.36f, 0.12f, 1.0f}, {0.22f, 0.44f, 0.15f, 1.0f},
          {0.12f, 0.31f, 0.10f, 1.0f}};
      for (int part = 0; part < 3; ++part) {
        float angle = yaw + static_cast<float>(part) * 2.094f;
        float offset = 0.34f * scale;
        createPiece(
            sphereMesh, leafColors[part],
            {x + std::cos(angle) * offset,
             terrainHeight + trunkHeight + (0.18f + part * 0.10f) * scale,
             z + std::sin(angle) * offset},
            {(1.05f - part * 0.08f) * scale, 0.85f * scale,
             (1.00f - part * 0.05f) * scale},
            0.0f, angle, 0.0f);
      }
    } else if (biome == 1) {
      int rockCount = 2 + static_cast<int>(distScale(rng) > 1.0f);
      for (int part = 0; part < rockCount; ++part) {
        float ox = distOffset(rng) * 0.55f * scale;
        float oz = distOffset(rng) * 0.55f * scale;
        float pieceScale = scale * (0.55f + 0.22f * part);
        float pieceHeight = GetHeight(x + ox, z + oz);
        XMFLOAT4 rockColor{0.68f, 0.58f, 0.42f, 1.0f};
        if (part == 0) {
          rockColor = {0.58f, 0.48f, 0.34f, 1.0f};
        }
        createPiece(rockMesh, rockColor,
                    {x + ox, pieceHeight + pieceScale * 0.34f, z + oz},
                    {pieceScale * 1.15f, pieceScale * 0.75f, pieceScale},
                    distOffset(rng) * 0.18f, yaw + part * 0.7f,
                    distOffset(rng) * 0.16f);
      }
    } else if (biome == 2) {
      for (int part = 0; part < 3; ++part) {
        float angle = yaw + static_cast<float>(part) * 2.094f;
        float distance = 0.38f * scale;
        if (part == 0) {
          distance = 0.0f;
        }
        float pieceHeight = GetHeight(x + std::cos(angle) * distance,
                                      z + std::sin(angle) * distance);
        float pieceScale = scale * (1.0f - part * 0.16f);
        createPiece(
            rockMesh, {0.70f + part * 0.05f, 0.86f + part * 0.03f, 0.96f, 1.0f},
            {x + std::cos(angle) * distance,
             pieceHeight + pieceScale * 0.60f,
             z + std::sin(angle) * distance},
            {pieceScale * 0.55f, pieceScale * 1.45f,
             pieceScale * 0.50f},
            distOffset(rng) * 0.22f, angle, distOffset(rng) * 0.18f);
      }
    } else if (biome == 3) {
      for (int part = 0; part < 3; ++part) {
        float angle = yaw + static_cast<float>(part) * 2.094f;
        float distance = 0.42f * part * scale;
        float px = x + std::cos(angle) * distance;
        float pz = z + std::sin(angle) * distance;
        float pieceScale = scale * (1.0f - part * 0.14f);
        XMFLOAT4 rockColor{0.43f, 0.40f, 0.38f, 1.0f};
        if (part == 0) {
          rockColor = {0.34f, 0.32f, 0.31f, 1.0f};
        }
        createPiece(
            rockMesh, rockColor,
            {px, GetHeight(px, pz) + pieceScale * 0.34f, pz},
            {pieceScale * 1.20f, pieceScale * 0.82f, pieceScale},
            distOffset(rng) * 0.20f, angle, distOffset(rng) * 0.20f);
      }
    }
  }

  LOG_INFO("WikiTerrain", "Created {} decoration clusters for biome {}",
           createdClusters, biome);
}

} // namespace game::systems

