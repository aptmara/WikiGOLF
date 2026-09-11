#pragma once
/**
 * @file Mesh.h
 * @brief 頂点/インデックスバッファ管理
*/

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <cstdint>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

namespace graphics {

using Microsoft::WRL::ComPtr;

/** @brief 頂点構造体 */
struct Vertex {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT2 texCoord;
  DirectX::XMFLOAT4 color;
  DirectX::XMFLOAT3 tangent{1.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 bitangent{0.0f, 1.0f, 0.0f};
};

/** @brief メッシュクラス */
class Mesh {
public:
  Mesh() = default;
  ~Mesh() = default;

  /** @brief メッシュを作成 */
  bool Create(ID3D11Device *device, const std::vector<Vertex> &vertices,
              const std::vector<uint32_t> &indices);

  /**
   * @brief 頂点バッファをCPUから毎フレーム更新可能な動的メッシュとして作成する
   * @details CPUスキニング(SkeletalModel::ComputePose)の結果を書き込む用途を想定。
   *          インデックスバッファは静的（Create()と同様）。
  */
  bool CreateDynamic(ID3D11Device *device,
                     const std::vector<Vertex> &initialVertices,
                     const std::vector<uint32_t> &indices);

  /**
   * @brief 動的頂点バッファの内容を更新する(CreateDynamic()で作成したメッシュ専用)
   * @param vertices 頂点数はCreateDynamic()時と同じであること
  */
  bool UpdateVertices(ID3D11DeviceContext *context,
                      const std::vector<Vertex> &vertices);

  /** @brief 描画用にバインド */
  void Bind(ID3D11DeviceContext *context) const;

  /** @brief 描画 */
  void Draw(ID3D11DeviceContext *context) const;

  /** @brief 有効かどうか */
  bool IsValid() const { return m_vertexBuffer && m_indexBuffer; }

  /** @brief インデックス数 */
  uint32_t GetIndexCount() const { return m_indexCount; }

  /** @brief フラスタムカリング用のローカル境界球 */
  const DirectX::BoundingSphere &GetBounds() const { return m_bounds; }

private:
  ComPtr<ID3D11Buffer> m_vertexBuffer;
  ComPtr<ID3D11Buffer> m_indexBuffer;
  uint32_t m_indexCount = 0;
  uint32_t m_stride = sizeof(Vertex);
  uint32_t m_offset = 0;
  uint32_t m_vertexCapacity = 0; ///< CreateDynamic()時の頂点数（更新時の検証用）
  DirectX::BoundingSphere m_bounds;
};

} // namespace graphics
