#include "DebugColliderGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace game::debug {
namespace {

DirectX::XMFLOAT3 TransformPoint(const DirectX::XMFLOAT3 &point,
                                 const DirectX::XMFLOAT3 &center,
                                 const DirectX::XMFLOAT4 &rotation) {
  using namespace DirectX;
  const XMVECTOR rotated =
      XMVector3Rotate(XMLoadFloat3(&point), XMLoadFloat4(&rotation));
  XMFLOAT3 result;
  XMStoreFloat3(&result,
                XMVectorAdd(rotated, XMLoadFloat3(&center)));
  return result;
}

} // namespace

void AppendSphereLines(std::vector<DebugLine3D> &lines,
                       const DirectX::XMFLOAT3 &center, float radius,
                       int segments) {
  using namespace DirectX;
  segments = (std::max)(segments, 3);
  for (int axis = 0; axis < 3; ++axis) {
    for (int index = 0; index < segments; ++index) {
      const float angleA = XM_2PI * index / segments;
      const float angleB = XM_2PI * (index + 1) / segments;
      XMFLOAT3 a = center;
      XMFLOAT3 b = center;
      const int first = (axis + 1) % 3;
      const int second = (axis + 2) % 3;
      reinterpret_cast<float *>(&a)[first] += std::cos(angleA) * radius;
      reinterpret_cast<float *>(&a)[second] += std::sin(angleA) * radius;
      reinterpret_cast<float *>(&b)[first] += std::cos(angleB) * radius;
      reinterpret_cast<float *>(&b)[second] += std::sin(angleB) * radius;
      lines.push_back({a, b});
    }
  }
}

void AppendBoxLines(std::vector<DebugLine3D> &lines,
                    const DirectX::XMFLOAT3 &center,
                    const DirectX::XMFLOAT3 &size,
                    const DirectX::XMFLOAT4 &rotation) {
  const DirectX::XMFLOAT3 half = {size.x * 0.5f, size.y * 0.5f,
                                  size.z * 0.5f};
  std::array<DirectX::XMFLOAT3, 8> corners = {
      DirectX::XMFLOAT3{-half.x, -half.y, -half.z},
      DirectX::XMFLOAT3{half.x, -half.y, -half.z},
      DirectX::XMFLOAT3{half.x, half.y, -half.z},
      DirectX::XMFLOAT3{-half.x, half.y, -half.z},
      DirectX::XMFLOAT3{-half.x, -half.y, half.z},
      DirectX::XMFLOAT3{half.x, -half.y, half.z},
      DirectX::XMFLOAT3{half.x, half.y, half.z},
      DirectX::XMFLOAT3{-half.x, half.y, half.z}};
  for (auto &corner : corners) {
    corner = TransformPoint(corner, center, rotation);
  }
  constexpr std::array<std::array<int, 2>, 12> edges = {
      std::array<int, 2>{0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const auto &edge : edges) {
    lines.push_back({corners[edge[0]], corners[edge[1]]});
  }
}

void AppendCylinderLines(std::vector<DebugLine3D> &lines,
                         const DirectX::XMFLOAT3 &center, float radius,
                         float height, const DirectX::XMFLOAT4 &rotation,
                         int segments) {
  using namespace DirectX;
  segments = (std::max)(segments, 3);
  const float halfHeight = height * 0.5f;
  for (int index = 0; index < segments; ++index) {
    const float angleA = XM_2PI * index / segments;
    const float angleB = XM_2PI * (index + 1) / segments;
    const XMFLOAT3 bottomA =
        TransformPoint({std::cos(angleA) * radius, -halfHeight,
                        std::sin(angleA) * radius}, center, rotation);
    const XMFLOAT3 bottomB =
        TransformPoint({std::cos(angleB) * radius, -halfHeight,
                        std::sin(angleB) * radius}, center, rotation);
    const XMFLOAT3 topA =
        TransformPoint({std::cos(angleA) * radius, halfHeight,
                        std::sin(angleA) * radius}, center, rotation);
    const XMFLOAT3 topB =
        TransformPoint({std::cos(angleB) * radius, halfHeight,
                        std::sin(angleB) * radius}, center, rotation);
    lines.push_back({bottomA, bottomB});
    lines.push_back({topA, topB});
    if (index % (std::max)(1, segments / 8) == 0) {
      lines.push_back({bottomA, topA});
    }
  }
}

void AppendVectorArrow(std::vector<DebugLine3D> &lines,
                       const DirectX::XMFLOAT3 &origin,
                       const DirectX::XMFLOAT3 &vector, float scale) {
  using namespace DirectX;
  const XMVECTOR direction = XMLoadFloat3(&vector);
  const float length = XMVectorGetX(XMVector3Length(direction));
  if (length < 0.0001f || scale <= 0.0f) {
    return;
  }
  const XMVECTOR start = XMLoadFloat3(&origin);
  const XMVECTOR unit = XMVectorScale(direction, 1.0f / length);
  const XMVECTOR end = XMVectorAdd(start, XMVectorScale(direction, scale));
  XMVECTOR side = XMVector3Cross(unit, XMVectorSet(0, 1, 0, 0));
  if (XMVectorGetX(XMVector3LengthSq(side)) < 0.0001f) {
    side = XMVector3Cross(unit, XMVectorSet(1, 0, 0, 0));
  }
  side = XMVector3Normalize(side);
  const float headLength = (std::min)(length * scale * 0.3f, 1.0f);
  const XMVECTOR headBase = XMVectorSubtract(end, XMVectorScale(unit, headLength));
  const XMVECTOR headSide = XMVectorScale(side, headLength * 0.5f);

  DebugLine3D shaft;
  DebugLine3D left;
  DebugLine3D right;
  XMStoreFloat3(&shaft.from, start);
  XMStoreFloat3(&shaft.to, end);
  XMStoreFloat3(&left.from, end);
  XMStoreFloat3(&left.to, XMVectorAdd(headBase, headSide));
  XMStoreFloat3(&right.from, end);
  XMStoreFloat3(&right.to, XMVectorSubtract(headBase, headSide));
  lines.push_back(shaft);
  lines.push_back(left);
  lines.push_back(right);
}

void AppendTerrainHeightfieldLines(std::vector<DebugLine3D> &lines,
                                   const std::vector<float> &heights,
                                   int resolutionX, int resolutionZ,
                                   float width, float depth,
                                   const DirectX::XMFLOAT3 &origin,
                                   float heightOffset, int stride) {
  if (resolutionX < 2 || resolutionZ < 2 ||
      heights.size() < static_cast<size_t>(resolutionX * resolutionZ)) {
    return;
  }
  stride = (std::max)(stride, 1);
  const auto point = [&](int x, int z) {
    const int clampedX = (std::min)(x, resolutionX - 1);
    const int clampedZ = (std::min)(z, resolutionZ - 1);
    return DirectX::XMFLOAT3{
        origin.x - width * 0.5f +
            width * clampedX / static_cast<float>(resolutionX - 1),
        origin.y + heights[clampedZ * resolutionX + clampedX] + heightOffset,
        origin.z + depth * 0.5f -
            depth * clampedZ / static_cast<float>(resolutionZ - 1)};
  };
  for (int z = 0; z < resolutionZ; z += stride) {
    for (int x = 0; x < resolutionX - 1; x += stride) {
      lines.push_back({point(x, z), point(x + stride, z)});
    }
  }
  if ((resolutionZ - 1) % stride != 0) {
    const int z = resolutionZ - 1;
    for (int x = 0; x < resolutionX - 1; x += stride) {
      lines.push_back({point(x, z), point(x + stride, z)});
    }
  }
  for (int x = 0; x < resolutionX; x += stride) {
    for (int z = 0; z < resolutionZ - 1; z += stride) {
      lines.push_back({point(x, z), point(x, z + stride)});
    }
  }
  if ((resolutionX - 1) % stride != 0) {
    const int x = resolutionX - 1;
    for (int z = 0; z < resolutionZ - 1; z += stride) {
      lines.push_back({point(x, z), point(x, z + stride)});
    }
  }
}

} // namespace game::debug
