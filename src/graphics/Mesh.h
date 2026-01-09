#pragma once
/**
 * @file Mesh.h
 * @brief 頂点/インデックスバッファ管理
 */

#include <DirectXMath.h>
#include <cstdint>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief 頂点構造体
struct Vertex {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT2 texCoord;
  DirectX::XMFLOAT4 color;
};

/// @brief メッシュクラス
class Mesh {
public:
  Mesh() = default;
  ~Mesh() = default;

  /// @brief メッシュを作成
  bool Create(ID3D11Device *device, const std::vector<Vertex> &vertices,
              const std::vector<uint32_t> &indices);

  /// @brief 描画用にバインド
  void Bind(ID3D11DeviceContext *context) const;

  /// @brief 描画
  void Draw(ID3D11DeviceContext *context) const;

  /// @brief 有効かどうか
  bool IsValid() const { return m_vertexBuffer && m_indexBuffer; }

  /// @brief インデックス数
  uint32_t GetIndexCount() const { return m_indexCount; }

private:
  ComPtr<ID3D11Buffer> m_vertexBuffer;
  ComPtr<ID3D11Buffer> m_indexBuffer;
  uint32_t m_indexCount = 0;
  uint32_t m_stride = sizeof(Vertex);
  uint32_t m_offset = 0;
};

} // namespace graphics
