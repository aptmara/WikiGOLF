#pragma once

#include <DirectXMath.h>
#include <vector>

namespace game::debug {

struct DebugLine3D {
  DirectX::XMFLOAT3 from;
  DirectX::XMFLOAT3 to;
};

void AppendSphereLines(std::vector<DebugLine3D> &lines,
                       const DirectX::XMFLOAT3 &center, float radius,
                       int segments = 24);
void AppendBoxLines(std::vector<DebugLine3D> &lines,
                    const DirectX::XMFLOAT3 &center,
                    const DirectX::XMFLOAT3 &size,
                    const DirectX::XMFLOAT4 &rotation);
void AppendCylinderLines(std::vector<DebugLine3D> &lines,
                         const DirectX::XMFLOAT3 &center, float radius,
                         float height, const DirectX::XMFLOAT4 &rotation,
                         int segments = 24);
void AppendVectorArrow(std::vector<DebugLine3D> &lines,
                       const DirectX::XMFLOAT3 &origin,
                       const DirectX::XMFLOAT3 &vector, float scale);
void AppendTerrainHeightfieldLines(std::vector<DebugLine3D> &lines,
                                   const std::vector<float> &heights,
                                   int resolutionX, int resolutionZ,
                                   float width, float depth,
                                   const DirectX::XMFLOAT3 &origin,
                                   float heightOffset, int stride);

} // namespace game::debug
