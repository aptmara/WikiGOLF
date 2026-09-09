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

} // namespace game::debug
