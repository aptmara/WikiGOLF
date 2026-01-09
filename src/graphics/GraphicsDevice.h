#pragma once
/**
 * @file GraphicsDevice.h
 * @brief DirectX11デバイス・コンテキスト管理
 */

#include <DirectXMath.h>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief DirectX11グラフィックスデバイス
class GraphicsDevice {
public:
  GraphicsDevice() = default;
  ~GraphicsDevice() = default;

  // コピー禁止
  GraphicsDevice(const GraphicsDevice &) = delete;
  GraphicsDevice &operator=(const GraphicsDevice &) = delete;

  /// @brief 初期化
  /// @param hWnd ウィンドウハンドル
  /// @param width ウィンドウ幅
  /// @param height ウィンドウ高さ
  /// @return 成功ならtrue
  bool Initialize(HWND hWnd, uint32_t width, uint32_t height);

  /// @brief シャットダウン
  void Shutdown();

  /// @brief フレーム開始（レンダーターゲットクリア）
  void BeginFrame(float r = 0.1f, float g = 0.1f, float b = 0.2f,
                  float a = 1.0f);

  /// @brief フレーム終了（Present）
  void EndFrame();

  /// @brief ウィンドウリサイズ
  bool Resize(uint32_t width, uint32_t height);

  // アクセサ
  ID3D11Device *GetDevice() const { return m_device.Get(); }
  ID3D11DeviceContext *GetContext() const { return m_context.Get(); }
  IDXGISwapChain *GetSwapChain() const { return m_swapChain.Get(); }
  uint32_t GetWidth() const { return m_width; }
  uint32_t GetHeight() const { return m_height; }
  float GetAspectRatio() const {
    return static_cast<float>(m_width) / static_cast<float>(m_height);
  }

private:
  bool CreateSwapChainAndDevice(HWND hWnd);
  bool CreateRenderTargetView();
  bool CreateDepthStencilView();
  void SetupViewport();
  void SetupRenderState();

private:
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;
  ComPtr<IDXGISwapChain> m_swapChain;
  ComPtr<ID3D11RenderTargetView> m_renderTargetView;
  ComPtr<ID3D11DepthStencilView> m_depthStencilView;
  ComPtr<ID3D11Texture2D> m_depthStencilBuffer;

  // States
  ComPtr<ID3D11RasterizerState> m_rasterizerState;
  ComPtr<ID3D11DepthStencilState> m_depthStencilState;

  uint32_t m_width = 0;
  uint32_t m_height = 0;
};

} // namespace graphics
