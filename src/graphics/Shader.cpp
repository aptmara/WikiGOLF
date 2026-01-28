/**
 * @file Shader.cpp
 * @brief シェーダーコンパイル・管理の実装
 */

#include "Shader.h"
#include "../core/Logger.h"

namespace graphics {

bool Shader::LoadFromFile(
    ID3D11Device *device, const std::wstring &vsPath,
    const std::string &vsEntry, const std::wstring &psPath,
    const std::string &psEntry,
    const std::vector<D3D11_INPUT_ELEMENT_DESC> &inputLayout) {
  UINT compileFlags = 0;
#ifdef _DEBUG
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  // 頂点シェーダーコンパイル
  ComPtr<ID3DBlob> vsBlob;
  ComPtr<ID3DBlob> errorBlob;
  HRESULT hr = D3DCompileFromFile(
      vsPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
      vsEntry.c_str(), "vs_5_0", compileFlags, 0, &vsBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) {
      LOG_ERROR("Shader", "VS Compile Error: {}",
                static_cast<const char *>(errorBlob->GetBufferPointer()));
    }
    return false;
  }

  hr = device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                  vsBlob->GetBufferSize(), nullptr,
                                  &m_vertexShader);
  if (FAILED(hr))
    return false;

  // ピクセルシェーダーコンパイル
  ComPtr<ID3DBlob> psBlob;
  hr = D3DCompileFromFile(psPath.c_str(), nullptr,
                          D3D_COMPILE_STANDARD_FILE_INCLUDE, psEntry.c_str(),
                          "ps_5_0", compileFlags, 0, &psBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) {
      LOG_ERROR("Shader", "PS Compile Error: {}",
                static_cast<const char *>(errorBlob->GetBufferPointer()));
    }
    return false;
  }

  hr = device->CreatePixelShader(psBlob->GetBufferPointer(),
                                 psBlob->GetBufferSize(), nullptr,
                                 &m_pixelShader);
  if (FAILED(hr))
    return false;

  // 入力レイアウト作成
  hr = device->CreateInputLayout(
      inputLayout.data(), static_cast<UINT>(inputLayout.size()),
      vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);

  return SUCCEEDED(hr);
}

void Shader::Bind(ID3D11DeviceContext *context) const {
  context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
  context->IASetInputLayout(m_inputLayout.Get());
}

std::vector<D3D11_INPUT_ELEMENT_DESC> Shader::GetDefaultInputLayout() {
  return {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 60,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
}

} // namespace graphics
