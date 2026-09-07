/**
 * @file PhysicsSystemCollision.cpp
 * @brief 物理更新で利用する安全な演算と衝突判定
*/
#include "PhysicsSystemInternals.h"
#include <algorithm>
#include <cmath>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

bool IsNaN(float v) { return v != v; }

/**
 * @brief ベクトルがNaNを含むかチェチ（��
*/
bool IsVectorNaN(XMVECTOR v) {
  float x = XMVectorGetX(v);
  float y = XMVectorGetY(v);
  float z = XMVectorGetZ(v);
  return IsNaN(x) || IsNaN(y) || IsNaN(z);
}

/**
 * @brief 安�（なベクトル正規化�（�ゼロベクトル対策）
*/
XMVECTOR SafeNormalize(XMVECTOR v, XMVECTOR fallback) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0001f) {
    return fallback;
  }
  return XMVector3Normalize(v);
}

/**
 * @brief 値を安�（な範囲��にクランチ（
*/
float SafeClamp(float v, float minVal, float maxVal) {
  if (IsNaN(v))
    return 0.0f;
  return std::clamp(v, minVal, maxVal);
}

/**
 * @brief ベクトルの長さを安�（に取得
*/
float SafeLength(XMVECTOR v) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0f || IsNaN(lenSq))
    return 0.0f;
  return std::sqrt(lenSq);
}

float SafeLengthSq(XMVECTOR v) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0f || IsNaN(lenSq))
    return 0.0f;
  return lenSq;
}


bool CheckSphereOBB(const XMFLOAT3 &spherePos, float radius,
                           const XMFLOAT3 &boxPos, const XMFLOAT3 &boxSize,
                           const XMFLOAT4 &boxRot, XMVECTOR &outNormal,
                           float &outDepth) {
  XMVECTOR sPos = XMLoadFloat3(&spherePos);
  XMVECTOR bPos = XMLoadFloat3(&boxPos);
  // ボックスのサイズ情報から半サイズ（ハーフエクステント）を算出
  XMVECTOR bHalf = XMVectorScale(XMLoadFloat3(&boxSize), 0.5f);
  XMVECTOR bRot = XMLoadFloat4(&boxRot);

  // 球をボックスのローカル座標系に変換
  XMVECTOR relPos = XMVectorSubtract(sPos, bPos);
  XMVECTOR invRot = XMQuaternionInverse(bRot);
  XMVECTOR localPos = XMVector3Rotate(relPos, invRot);

  // ローカル座標系でのAABB判定（クランプ）
  XMVECTOR closestLocal = XMVectorClamp(localPos, XMVectorNegate(bHalf), bHalf);

  // 距離チェック
  XMVECTOR distVecLocal = XMVectorSubtract(localPos, closestLocal);
  float d2 = XMVectorGetX(XMVector3LengthSq(distVecLocal));

  // 中心が外側にある場合
  if (d2 > 0.00001f) {
    if (d2 > radius * radius) {
      return false; // 衝突なし
    }

    float d = std::sqrt(d2);
    XMVECTOR localNormal = XMVectorScale(distVecLocal, 1.0f / d);
    outNormal = XMVector3Rotate(localNormal, bRot);
    outDepth = radius - d;
    return true;
  }

  // 中心が内部にある場合：最も近い面を探す
  float x = XMVectorGetX(localPos);
  float y = XMVectorGetY(localPos);
  float z = XMVectorGetZ(localPos);
  float hx = XMVectorGetX(bHalf);
  float hy = XMVectorGetY(bHalf);
  float hz = XMVectorGetZ(bHalf);

  // 各面への距離（正: 内側への距離）
  float dx_p = hx - x; // +X face
  float dx_n = x + hx; // -X face
  float dy_p = hy - y; // +Y face
  float dy_n = y + hy; // -Y face
  float dz_p = hz - z; // +Z face
  float dz_n = z + hz; // -Z face

  // 最小の絶対値を持つ軸を探す（そこが最も浅い脱出ルート）
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
  // 脱出方向は面法線
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
  // 貫通深度 = (表面までの距離) + 半径
  // minDは「表面までの距離」
  outDepth = minD + radius;

  return true;
}


TerrainSample SampleTerrainAt(const TerrainData &terrain, float x,
                                     float z) {
  TerrainSample sample;
  float width = terrain.config.worldWidth;
  float depth = terrain.config.worldDepth;
  int resX = terrain.config.resolutionX;
  int resZ = terrain.config.resolutionZ;

  float u = (x / width) + 0.5f;
  float v = 0.5f - (z / depth);
  if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f || resX < 2 ||
      resZ < 2) {
    return sample;
  }

  float fx = u * (resX - 1);
  float fz = v * (resZ - 1);
  int ix = std::clamp(static_cast<int>(fx), 0, resX - 2);
  int iz = std::clamp(static_cast<int>(fz), 0, resZ - 2);
  float dx = fx - ix;
  float dz = fz - iz;

  auto getHeightSafe = [&](int gx, int gz) -> float {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.heightMap.size())) {
      return terrain.heightMap[idx];
    }
    return 0.0f;
  };
  auto getNormalSafe = [&](int gx, int gz) -> XMVECTOR {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.normals.size())) {
      XMVECTOR n = XMLoadFloat3(&terrain.normals[idx]);
      if (!IsVectorNaN(n))
        return n;
    }
    return XMVectorSet(0, 1, 0, 0);
  };

  float h00 = getHeightSafe(ix, iz);
  float h10 = getHeightSafe(ix + 1, iz);
  float h01 = getHeightSafe(ix, iz + 1);
  float h11 = getHeightSafe(ix + 1, iz + 1);
  float h0 = h00 * (1.0f - dx) + h10 * dx;
  float h1 = h01 * (1.0f - dx) + h11 * dx;
  sample.height = h0 * (1.0f - dz) + h1 * dz;
  if (IsNaN(sample.height)) {
    sample.height = 0.0f;
  }

  XMVECTOR n0 = XMVectorLerp(getNormalSafe(ix, iz), getNormalSafe(ix + 1, iz),
                             dx);
  XMVECTOR n1 = XMVectorLerp(getNormalSafe(ix, iz + 1),
                             getNormalSafe(ix + 1, iz + 1), dx);
  sample.normal = SafeNormalize(XMVectorLerp(n0, n1, dz));

  int matX = std::clamp(static_cast<int>(u * (resX - 1)), 0, resX - 1);
  int matZ = std::clamp(static_cast<int>(v * (resZ - 1)), 0, resZ - 1);
  int matIdx = matZ * resX + matX;
  if (matIdx >= 0 && matIdx < static_cast<int>(terrain.materialMap.size())) {
    sample.material = terrain.materialMap[matIdx];
  }
  sample.valid = true;
  return sample;
}


} // namespace game::systems
