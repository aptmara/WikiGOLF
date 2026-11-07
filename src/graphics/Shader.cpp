/**
 * @file Shader.cpp
 * @brief シェーダーコンパイル・管理の実装
*/

#include "Shader.h"
#include "../core/Logger.h"
#include "ShaderPrecompiled.h"
#include <cstring>

namespace graphics {
namespace {

/**
 * @brief シェーダーのバイトコードを得る。ビルド時にコンパイル済みのものがあればそれを使う。
 * @details 済みのものはゲームと同じ関数・同じフラグで作っているので中身は同じ。
 *          デバッグビルドはフラグが違うので常にコンパイルする。
 */
HRESULT CompileOrLoad(const std::wstring &path, const std::string &entry,
                      const char *profile, UINT flags, ComPtr<ID3DBlob> &blob,
                      ComPtr<ID3DBlob> &errorBlob) {
#ifndef _DEBUG
  if (auto bytes = shader_precompiled::Load(path, entry)) {
    if (SUCCEEDED(D3DCreateBlob(bytes->size(), &blob))) {
      std::memcpy(blob->GetBufferPointer(), bytes->data(), bytes->size());
      return S_OK;
    }
  }
#endif
  return D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            entry.c_str(), profile, flags, 0, &blob, &errorBlob);
}

} // namespace

bool LoadShaderBytecode(const std::wstring &path, const char *profile,
                        ComPtr<ID3DBlob> &blob) {
  UINT compileFlags = 0;
#ifdef _DEBUG
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
  ComPtr<ID3DBlob> errorBlob;
  const HRESULT hr =
      CompileOrLoad(path, "main", profile, compileFlags, blob, errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) {
      LOG_ERROR("Shader", "{} Compile Error: {}", profile,
                static_cast<const char *>(errorBlob->GetBufferPointer()));
    } else {
      LOG_ERROR("Shader", "{} Compile Error: hr=0x{:08X}", profile,
                static_cast<unsigned>(hr));
    }
    return false;
  }
  return true;
}

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
  HRESULT hr =
      CompileOrLoad(vsPath, vsEntry, "vs_5_0", compileFlags, vsBlob, errorBlob);
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
  hr = CompileOrLoad(psPath, psEntry, "ps_5_0", compileFlags, psBlob, errorBlob);
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
