#include "TangentGenerator.h"
#include <DirectXMath.h>
#include <cmath>

namespace graphics {

using namespace DirectX;

void ComputeTangents(std::vector<Vertex> &vertices,
                     const std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.size() < 3)
    return;

  for (auto &v : vertices) {
    v.tangent = {0.0f, 0.0f, 0.0f};
    v.bitangent = {0.0f, 0.0f, 0.0f};
  }

  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Vertex &v0 = vertices[indices[i]];
    Vertex &v1 = vertices[indices[i + 1]];
    Vertex &v2 = vertices[indices[i + 2]];

    XMFLOAT3 p0 = v0.position;
    XMFLOAT3 p1 = v1.position;
    XMFLOAT3 p2 = v2.position;
    XMFLOAT2 uv0 = v0.texCoord;
    XMFLOAT2 uv1 = v1.texCoord;
    XMFLOAT2 uv2 = v2.texCoord;

    float x1 = p1.x - p0.x;
    float y1 = p1.y - p0.y;
    float z1 = p1.z - p0.z;
    float x2 = p2.x - p0.x;
    float y2 = p2.y - p0.y;
    float z2 = p2.z - p0.z;

    float s1 = uv1.x - uv0.x;
    float t1 = uv1.y - uv0.y;
    float s2 = uv2.x - uv0.x;
    float t2 = uv2.y - uv0.y;

    float denom = (s1 * t2 - s2 * t1);
    if (std::abs(denom) < 1e-6f) {
      continue;
    }
    float r = 1.0f / denom;

    XMFLOAT3 tangent{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,
                     (t2 * z1 - t1 * z2) * r};
    XMFLOAT3 bitangent{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
                       (s1 * z2 - s2 * z1) * r};

    auto accum = [](XMFLOAT3 &dst, const XMFLOAT3 &src) {
      dst.x += src.x;
      dst.y += src.y;
      dst.z += src.z;
    };

    accum(v0.tangent, tangent);
    accum(v1.tangent, tangent);
    accum(v2.tangent, tangent);

    accum(v0.bitangent, bitangent);
    accum(v1.bitangent, bitangent);
    accum(v2.bitangent, bitangent);
  }

  for (auto &v : vertices) {
    XMVECTOR n = XMLoadFloat3(&v.normal);
    XMVECTOR t = XMLoadFloat3(&v.tangent);
    XMVECTOR b = XMLoadFloat3(&v.bitangent);

    // オルソ化
    t = XMVector3Normalize(t - n * XMVector3Dot(n, t));
    b = XMVector3Normalize(b - n * XMVector3Dot(n, b));

    // ビタングルの向き補正
    XMVECTOR c = XMVector3Cross(n, t);
    float handedness = XMVectorGetX(XMVector3Dot(c, b)) < 0.0f ? -1.0f : 1.0f;
    b = XMVector3Normalize(XMVector3Cross(n, t) * handedness);

    XMStoreFloat3(&v.tangent, t);
    XMStoreFloat3(&v.bitangent, b);
  }
}

} // namespace graphics
