#pragma once
/**
 * @file GraphicsDevice.h
 * @brief DirectX11デバイス・コンテキスト管理
 */

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "../core/Profiler.h"

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

  /// @brief フレーム開始（GPU query開始・レンダーターゲットクリア）
  void BeginFrame(uint64_t profileFrameIndex, float r = 0.1f, float g = 0.1f,
                  float b = 0.2f,
                  float a = 1.0f);

  /// @brief フレーム終了（Present）
  void EndFrame();

  /// @brief 現在のGPUフレーム内でtimestamp区間を開始・終了します。
  void BeginGpuScope(std::string_view name);
  void EndGpuScope();

  /// @brief 非同期回収済みのGPU計測結果を取得します。
  std::vector<core::GpuFrameSample> ConsumeGpuProfileSamples();

  /// @brief ウィンドウリサイズ
  bool Resize(uint32_t width, uint32_t height);

  // アクセサ
  ID3D11Device *GetDevice() const { return m_device.Get(); }
  ID3D11DeviceContext *GetContext() const { return m_context.Get(); }
  IDXGISwapChain *GetSwapChain() const { return m_swapChain.Get(); }
  HRESULT GetDeviceRemovedReason() const {
    if (m_device) {
      return m_device->GetDeviceRemovedReason();
    }
    return E_FAIL;
  }
  D3D_DRIVER_TYPE GetDriverType() const { return m_driverType; }
  D3D_FEATURE_LEVEL GetFeatureLevel() const { return m_featureLevel; }
  const std::string &GetAdapterName() const { return m_adapterName; }
  uint64_t GetDedicatedVideoMemoryBytes() const {
    return m_dedicatedVideoMemoryBytes;
  }
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
  void CaptureAdapterInfo();
  bool InitializeGpuProfilerQueries();
  void ResolveGpuProfilerQueries();

  struct GpuTimestampQueries {
    std::string name;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> end;
  };

  struct GpuFrameQueries {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> pipeline;
    ComPtr<ID3D11Query> frameStart;
    ComPtr<ID3D11Query> frameEnd;
    std::vector<GpuTimestampQueries> scopes;
    uint64_t frameIndex = 0;
    size_t usedScopeCount = 0;
    bool issued = false;
  };

private:
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;
  ComPtr<IDXGISwapChain> m_swapChain;
  ComPtr<ID3D11RenderTargetView> m_renderTargetView;
  ComPtr<ID3D11DepthStencilView> m_depthStencilView;
  ComPtr<ID3D11Texture2D> m_depthStencilBuffer;

  // レンダラーステート
  ComPtr<ID3D11RasterizerState> m_rasterizerState;
  ComPtr<ID3D11DepthStencilState> m_depthStencilState;

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  D3D_DRIVER_TYPE m_driverType = D3D_DRIVER_TYPE_UNKNOWN;
  D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
  std::string m_adapterName = "Unknown";
  uint64_t m_dedicatedVideoMemoryBytes = 0;

  static constexpr size_t kGpuQueryBufferCount = 8;
  std::array<GpuFrameQueries, kGpuQueryBufferCount> m_gpuQueryFrames;
  size_t m_gpuQueryWriteIndex = 0;
  GpuFrameQueries *m_currentGpuQueryFrame = nullptr;
  std::vector<size_t> m_gpuScopeStack;
  std::vector<core::GpuFrameSample> m_readyGpuSamples;
  bool m_gpuProfilerAvailable = false;
};

class ScopedGpuTimer {
public:
  ScopedGpuTimer(GraphicsDevice &graphics, std::string_view name)
      : m_graphics(graphics) {
    m_graphics.BeginGpuScope(name);
  }
  ~ScopedGpuTimer() { m_graphics.EndGpuScope(); }

  ScopedGpuTimer(const ScopedGpuTimer &) = delete;
  ScopedGpuTimer &operator=(const ScopedGpuTimer &) = delete;

private:
  GraphicsDevice &m_graphics;
};

} // namespace graphics
