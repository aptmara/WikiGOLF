#pragma once
/**
 * @file Shader.h
 * @brief シェーダーコンパイル・管理
 */

#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <wrl/client.h>


#pragma comment(lib, "d3dcompiler.lib")

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief シェーダープログラム
class Shader {
public:
  Shader() = default;
  ~Shader() = default;

  /// @brief HLSLファイルからシェーダーを読み込み
  bool LoadFromFile(ID3D11Device *device, const std::wstring &vsPath,
                    const std::string &vsEntry, const std::wstring &psPath,
                    const std::string &psEntry,
                    const std::vector<D3D11_INPUT_ELEMENT_DESC> &inputLayout);

  /// @brief シェーダーをバインド
  void Bind(ID3D11DeviceContext *context) const;

  /// @brief 有効かどうか
  bool IsValid() const {
    return m_vertexShader && m_pixelShader && m_inputLayout;
  }

  /// @brief 標準的な入力レイアウトを取得
  static std::vector<D3D11_INPUT_ELEMENT_DESC> GetDefaultInputLayout();

private:
  ComPtr<ID3D11VertexShader> m_vertexShader;
  ComPtr<ID3D11PixelShader> m_pixelShader;
  ComPtr<ID3D11InputLayout> m_inputLayout;
};

} // namespace graphics
