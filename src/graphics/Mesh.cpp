/**
 * @file Mesh.cpp
 * @brief 頂点/インデックスバッファ管理の実装
 */

#include "Mesh.h"

namespace graphics {

bool Mesh::Create(ID3D11Device *device, const std::vector<Vertex> &vertices,
                  const std::vector<uint32_t> &indices) {
  // 頂点バッファ作成
  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.Usage = D3D11_USAGE_DEFAULT;
  vbDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA vbData = {};
  vbData.pSysMem = vertices.data();

  HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, &m_vertexBuffer);
  if (FAILED(hr))
    return false;

  // インデックスバッファ作成
  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.Usage = D3D11_USAGE_DEFAULT;
  ibDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * indices.size());
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

  D3D11_SUBRESOURCE_DATA ibData = {};
  ibData.pSysMem = indices.data();

  hr = device->CreateBuffer(&ibDesc, &ibData, &m_indexBuffer);
  if (FAILED(hr))
    return false;

  m_indexCount = static_cast<uint32_t>(indices.size());
  return true;
}

void Mesh::Bind(ID3D11DeviceContext *context) const {
  context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride,
                              &m_offset);
  context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Mesh::Draw(ID3D11DeviceContext *context) const {
  context->DrawIndexed(m_indexCount, 0, 0);
}

// Primitives moved to MeshPrimitives.cpp

} // namespace graphics
