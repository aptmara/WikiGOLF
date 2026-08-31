/**
 * @file GraphicsDevice.cpp
 * @brief DirectX11デバイス・コンテキスト管理の実装
 */

#include "GraphicsDevice.h"
#include "../core/Logger.h"
#include <algorithm>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <limits>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace graphics {
namespace {

std::string WideToUtf8(const wchar_t *value) {
  if (!value || value[0] == L'\0') {
    return "Unknown";
  }
  const int required =
      WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (required <= 1) {
    return "Unknown";
  }
  std::string result(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}

} // namespace

bool GraphicsDevice::Initialize(HWND hWnd, uint32_t width, uint32_t height) {
  m_width = width;
  m_height = height;
  m_renderWidth = width;
  m_renderHeight = height;

  if (!CreateSwapChainAndDevice(hWnd))
    return false;
  if (!CreateRenderTargetView())
    return false;
  if (!InitializePostProcessResources())
    return false;
  if (!CreateSceneRenderTargets())
    return false;
  SetupSceneViewport();

  m_gpuProfilerAvailable = InitializeGpuProfilerQueries();
  if (!m_gpuProfilerAvailable) {
    LOG_WARN("GraphicsDevice",
             "D3D11 GPU profiler queries are unavailable; CPU profiling will continue");
  }

  return true;
}

void GraphicsDevice::Shutdown() {
  // 排他フルスクリーンのままSwapChainを破棄するとDXGIが不正状態になるため、
  // 先にウィンドウモードへ戻す。
  if (m_isExclusiveFullscreen && m_swapChain) {
    m_swapChain->SetFullscreenState(FALSE, nullptr);
    m_isExclusiveFullscreen = false;
  }

  if (m_context) {
    m_context->ClearState();
  }
  m_currentGpuQueryFrame = nullptr;
  m_gpuScopeStack.clear();
  m_readyGpuSamples.clear();
  for (auto &frame : m_gpuQueryFrames) {
    frame.scopes.clear();
    frame.frameEnd.Reset();
    frame.frameStart.Reset();
    frame.pipeline.Reset();
    frame.disjoint.Reset();
  }
  m_rasterizerState.Reset();
  m_depthStencilState.Reset();
  m_depthStencilView.Reset();
  m_depthStencilBuffer.Reset();
  m_sceneColorTexMS.Reset();
  m_sceneColorRTVMS.Reset();
  m_sceneColorTexResolved.Reset();
  m_sceneColorRTVResolved.Reset();
  m_sceneColorSRVResolved.Reset();
  m_fxaaTex.Reset();
  m_fxaaRTV.Reset();
  m_fxaaSRV.Reset();
  m_fullscreenVB.Reset();
  m_linearSampler.Reset();
  m_fxaaConstantBuffer.Reset();
  m_postProcessBlendState.Reset();
  m_postProcessDepthState.Reset();
  m_postProcessRasterizerState.Reset();
  m_renderTargetView.Reset();
  m_swapChain.Reset();
  m_context.Reset();
  m_device.Reset();
}

void GraphicsDevice::BeginFrame(uint64_t profileFrameIndex, float r, float g,
                                float b, float a) {
  ResolveGpuProfilerQueries();
  m_gpuScopeStack.clear();
  m_currentGpuQueryFrame = nullptr;

  if (m_gpuProfilerAvailable) {
    auto &queryFrame = m_gpuQueryFrames[m_gpuQueryWriteIndex];
    if (!queryFrame.issued) {
      queryFrame.frameIndex = profileFrameIndex;
      queryFrame.usedScopeCount = 0;
      m_context->Begin(queryFrame.disjoint.Get());
      m_context->End(queryFrame.frameStart.Get());
      m_context->Begin(queryFrame.pipeline.Get());
      m_currentGpuQueryFrame = &queryFrame;
    }
  }

  // 3D描画（Skybox/メッシュ）は内部描画解像度のオフスクリーンターゲットへ描画する。
  // MSAA有効時はMSAAカラーターゲットへ、無効時は直接「解決済み」ターゲットへ描画する。
  ID3D11RenderTargetView *sceneRTV = (m_quality.msaaSamples > 1)
                                         ? m_sceneColorRTVMS.Get()
                                         : m_sceneColorRTVResolved.Get();

  float clearColor[4] = {r, g, b, a};
  m_context->ClearRenderTargetView(sceneRTV, clearColor);
  m_context->ClearDepthStencilView(m_depthStencilView.Get(),
                                   D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                   1.0f, 0);
  m_context->OMSetRenderTargets(1, &sceneRTV, m_depthStencilView.Get());
  SetupSceneViewport();
}

void GraphicsDevice::RunFullscreenPass(Shader &shader, ID3D11ShaderResourceView *srv,
                                       ID3D11RenderTargetView *dstRTV,
                                       uint32_t dstWidth, uint32_t dstHeight,
                                       bool useFxaaConstants) {
  if (!shader.IsValid() || !srv || !dstRTV) {
    return;
  }

  D3D11_VIEWPORT vp = {};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(dstWidth);
  vp.Height = static_cast<float>(dstHeight);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  m_context->RSSetViewports(1, &vp);
  m_context->OMSetRenderTargets(1, &dstRTV, nullptr);

  shader.Bind(m_context.Get());

  UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT2);
  UINT offset = 0;
  m_context->IASetVertexBuffers(0, 1, m_fullscreenVB.GetAddressOf(), &stride,
                                &offset);
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  m_context->PSSetShaderResources(0, 1, &srv);
  m_context->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

  if (useFxaaConstants) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_fxaaConstantBuffer.Get(), 0,
                                 D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      float *data = static_cast<float *>(mapped.pData);
      data[0] = (m_renderWidth > 0) ? 1.0f / static_cast<float>(m_renderWidth) : 0.0f;
      data[1] = (m_renderHeight > 0) ? 1.0f / static_cast<float>(m_renderHeight) : 0.0f;
      data[2] = 0.0f;
      data[3] = 0.0f;
      m_context->Unmap(m_fxaaConstantBuffer.Get(), 0);
    }
    m_context->PSSetConstantBuffers(0, 1, m_fxaaConstantBuffer.GetAddressOf());
  }

  m_context->RSSetState(m_postProcessRasterizerState.Get());
  m_context->OMSetBlendState(m_postProcessBlendState.Get(), nullptr, 0xFFFFFFFF);
  m_context->OMSetDepthStencilState(m_postProcessDepthState.Get(), 0);

  m_context->Draw(3, 0);

  ID3D11ShaderResourceView *nullSRV = nullptr;
  m_context->PSSetShaderResources(0, 1, &nullSRV);
}

void GraphicsDevice::ResolveSceneToBackbuffer() {
  if (m_quality.msaaSamples > 1 && m_sceneColorTexMS && m_sceneColorTexResolved) {
    m_context->ResolveSubresource(m_sceneColorTexResolved.Get(), 0,
                                  m_sceneColorTexMS.Get(), 0,
                                  DXGI_FORMAT_R8G8B8A8_UNORM);
  }

  ID3D11ShaderResourceView *sourceSRV = m_sceneColorSRVResolved.Get();

  if (m_quality.fxaaEnabled && m_fxaaRTV && m_fxaaSRV) {
    RunFullscreenPass(m_fxaaShader, m_sceneColorSRVResolved.Get(),
                      m_fxaaRTV.Get(), m_renderWidth, m_renderHeight, true);
    sourceSRV = m_fxaaSRV.Get();
  }

  // 内部描画解像度 → 出力(バックバッファ)解像度へアップスケール
  RunFullscreenPass(m_upscaleShader, sourceSRV, m_renderTargetView.Get(),
                    m_width, m_height, false);

  // 以降のUI(D2D)/ScreenFadeはバックバッファへ直接描画されるため、
  // 状態を出力解像度基準に戻しておく（深度テストは既定の有効状態へ復帰）。
  m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
  m_context->RSSetState(nullptr);
  m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
  m_context->OMSetDepthStencilState(nullptr, 0);
  SetupBackbufferViewport();
}

void GraphicsDevice::EndFrame() {
  if (m_currentGpuQueryFrame) {
    while (!m_gpuScopeStack.empty()) {
      EndGpuScope();
    }
    m_context->End(m_currentGpuQueryFrame->pipeline.Get());
    m_context->End(m_currentGpuQueryFrame->frameEnd.Get());
    m_context->End(m_currentGpuQueryFrame->disjoint.Get());
    m_currentGpuQueryFrame->issued = true;
    m_gpuQueryWriteIndex = (m_gpuQueryWriteIndex + 1) % kGpuQueryBufferCount;
    m_currentGpuQueryFrame = nullptr;
  }

  m_swapChain->Present(m_vsyncEnabled ? 1 : 0, 0);
  ResolveGpuProfilerQueries();
}

void GraphicsDevice::BeginGpuScope(std::string_view name) {
  m_gpuScopeStack.push_back(std::numeric_limits<size_t>::max());
  if (!m_currentGpuQueryFrame) {
    return;
  }

  const size_t scopeIndex = m_currentGpuQueryFrame->usedScopeCount++;
  if (scopeIndex >= m_currentGpuQueryFrame->scopes.size()) {
    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_TIMESTAMP;
    GpuTimestampQueries queries;
    if (FAILED(m_device->CreateQuery(&queryDesc, &queries.start)) ||
        FAILED(m_device->CreateQuery(&queryDesc, &queries.end))) {
      --m_currentGpuQueryFrame->usedScopeCount;
      return;
    }
    m_currentGpuQueryFrame->scopes.push_back(std::move(queries));
  }

  auto &queries = m_currentGpuQueryFrame->scopes[scopeIndex];
  queries.name = std::string(name);
  m_context->End(queries.start.Get());
  m_gpuScopeStack.back() = scopeIndex;
}

void GraphicsDevice::EndGpuScope() {
  if (m_gpuScopeStack.empty()) {
    return;
  }
  const size_t scopeIndex = m_gpuScopeStack.back();
  m_gpuScopeStack.pop_back();
  if (!m_currentGpuQueryFrame ||
      scopeIndex == std::numeric_limits<size_t>::max() ||
      scopeIndex >= m_currentGpuQueryFrame->scopes.size()) {
    return;
  }
  m_context->End(m_currentGpuQueryFrame->scopes[scopeIndex].end.Get());
}

std::vector<core::GpuFrameSample>
GraphicsDevice::ConsumeGpuProfileSamples() {
  std::vector<core::GpuFrameSample> result;
  result.swap(m_readyGpuSamples);
  return result;
}

bool GraphicsDevice::Resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return false;

  m_width = width;
  m_height = height;

  m_context->OMSetRenderTargets(0, nullptr, nullptr);
  m_renderTargetView.Reset();

  HRESULT hr =
      m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr))
    return false;

  if (!CreateRenderTargetView())
    return false;
  // 出力解像度が変わったため、内部描画解像度(renderWidth/Height)も併せて再計算する
  if (!CreateSceneRenderTargets())
    return false;
  SetupSceneViewport();
  SetupRenderState();

  return true;
}

void GraphicsDevice::ApplyQualitySettings(const QualitySettings &settings) {
  m_quality.renderScale = std::clamp(settings.renderScale, 0.5f, 1.0f);
  int samples = settings.msaaSamples;
  if (samples != 1 && samples != 2 && samples != 4 && samples != 8) {
    samples = 1;
  }
  m_quality.msaaSamples = samples;
  m_quality.fxaaEnabled = settings.fxaaEnabled;

  if (!CreateSceneRenderTargets()) {
    LOG_ERROR("GraphicsDevice",
              "ApplyQualitySettings: failed to recreate scene render targets");
    return;
  }
  SetupSceneViewport();
  SetupRenderState();

  LOG_INFO("GraphicsDevice",
           "Quality settings applied: renderScale={:.2f} ({}x{}) MSAA={}x FXAA={}",
           m_quality.renderScale, m_renderWidth, m_renderHeight,
           m_quality.msaaSamples, m_quality.fxaaEnabled);
}

bool GraphicsDevice::SetFullscreenExclusive(bool enable, uint32_t width,
                                            uint32_t height) {
  if (!m_swapChain) {
    return false;
  }

  if (enable) {
    DXGI_MODE_DESC mode = {};
    mode.Width = width;
    mode.Height = height;
    mode.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    mode.RefreshRate.Numerator = 0;
    mode.RefreshRate.Denominator = 0;
    mode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    mode.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

    HRESULT hr = m_swapChain->ResizeTarget(&mode);
    if (FAILED(hr)) {
      LOG_ERROR("GraphicsDevice",
               "SetFullscreenExclusive: ResizeTarget failed ({:08X})",
               static_cast<uint32_t>(hr));
      return false;
    }
    hr = m_swapChain->SetFullscreenState(TRUE, nullptr);
    if (FAILED(hr)) {
      LOG_ERROR("GraphicsDevice",
               "SetFullscreenExclusive: SetFullscreenState(TRUE) failed ({:08X})",
               static_cast<uint32_t>(hr));
      return false;
    }
    m_isExclusiveFullscreen = true;
    LOG_INFO("GraphicsDevice", "Entered exclusive fullscreen ({}x{})", width,
             height);
  } else {
    m_swapChain->SetFullscreenState(FALSE, nullptr);
    m_isExclusiveFullscreen = false;
    LOG_INFO("GraphicsDevice", "Exited exclusive fullscreen");
  }
  return true;
}

bool GraphicsDevice::CreateSwapChainAndDevice(HWND hWnd) {
  auto driverTypeToStr = [](D3D_DRIVER_TYPE type) {
    switch (type) {
    case D3D_DRIVER_TYPE_HARDWARE:
      return "HARDWARE";
    case D3D_DRIVER_TYPE_WARP:
      return "WARP";
    case D3D_DRIVER_TYPE_REFERENCE:
      return "REFERENCE";
    default:
      return "UNKNOWN";
    }
  };

  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  swapChainDesc.BufferCount = 2;
  swapChainDesc.BufferDesc.Width = m_width;
  swapChainDesc.BufferDesc.Height = m_height;
  swapChainDesc.BufferDesc.Format =
      DXGI_FORMAT_B8G8R8A8_UNORM; // D2D互換フォーマット
  swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
  swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.OutputWindow = hWnd;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.SampleDesc.Quality = 0;
  swapChainDesc.Windowed = TRUE;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL; // D2D互換を優先

  UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // D2D互換に必須
#ifdef _DEBUG
  createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };

  D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
  auto resetCreatedObjects = [&]() {
    m_context.Reset();
    m_device.Reset();
    m_swapChain.Reset();
  };
  auto tryCreateForAdapter = [&](IDXGIAdapter1 *adapter) {
    resetCreatedObjects();
    return D3D11CreateDeviceAndSwapChain(
        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, createDeviceFlags,
        featureLevels,
        _countof(featureLevels), D3D11_SDK_VERSION, &swapChainDesc,
        &m_swapChain, &m_device, &featureLevel, &m_context);
  };

  HRESULT hr = E_FAIL;
  ComPtr<IDXGIFactory1> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory.As(&factory6))) {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumResult = factory6->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));
        if (enumResult == DXGI_ERROR_NOT_FOUND) {
          break;
        }
        if (FAILED(enumResult)) {
          break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
          continue;
        }
        hr = tryCreateForAdapter(adapter.Get());
        if (SUCCEEDED(hr)) {
          break;
        }
      }
    } else {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumResult = factory->EnumAdapters1(index, &adapter);
        if (enumResult == DXGI_ERROR_NOT_FOUND) {
          break;
        }
        if (FAILED(enumResult)) {
          break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
          continue;
        }
        hr = tryCreateForAdapter(adapter.Get());
        if (SUCCEEDED(hr)) {
          break;
        }
      }
    }
  }

  if (SUCCEEDED(hr)) {
    m_driverType = D3D_DRIVER_TYPE_HARDWARE;
  } else {
    LOG_WARN("GraphicsDevice",
             "Hardware device creation failed (hr={:08X}); falling back to WARP",
             static_cast<uint32_t>(hr));
    resetCreatedObjects();
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
        featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
        &swapChainDesc, &m_swapChain, &m_device, &featureLevel, &m_context);
    m_driverType = D3D_DRIVER_TYPE_WARP;
  }

  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice", "Device creation failed (hr={:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  m_featureLevel = featureLevel;
  CaptureAdapterInfo();
  const char* driverStr = "WARP";
  if (m_driverType == D3D_DRIVER_TYPE_HARDWARE) {
    driverStr = "HARDWARE";
  }
  LOG_INFO("GraphicsDevice",
           "Device created. Driver={}, Adapter='{}', DedicatedVRAM={:.0f}MB, "
           "FeatureLevel=0x{:04X}",
           driverStr, m_adapterName,
           static_cast<double>(m_dedicatedVideoMemoryBytes) / (1024.0 * 1024.0),
           static_cast<uint32_t>(featureLevel));

  // D3D11でのマルチスレッド保護を有効化
  // デバイスコンテキストからマルチスレッド保護インターフェースを取得
  Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
  if (SUCCEEDED(m_context.As(&multithread))) {
    multithread->SetMultithreadProtected(TRUE);
    LOG_INFO("GraphicsDevice", "Enabled D3D11Multithread Protection on context");
  } else {
    // 古いWindows向けにD3D10のマルチスレッド保護も試行
    Microsoft::WRL::ComPtr<ID3D10Multithread> mt10;
    if (SUCCEEDED(m_device.As(&mt10))) {
      mt10->SetMultithreadProtected(TRUE);
      LOG_INFO("GraphicsDevice", "Enabled D3D10Multithread Protection on device");
    }
  }

  if (m_device) {
    HRESULT reason = m_device->GetDeviceRemovedReason();
    if (reason != S_OK) {
      LOG_ERROR("GraphicsDevice", "Device already removed (reason={:08X})",
                static_cast<uint32_t>(reason));
      return false;
    }
  }

  return true;
}

void GraphicsDevice::CaptureAdapterInfo() {
  m_adapterName = "Unknown";
  m_dedicatedVideoMemoryBytes = 0;
  if (!m_device) {
    return;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC description{};
  if (SUCCEEDED(m_device.As(&dxgiDevice)) &&
      SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
      SUCCEEDED(adapter->GetDesc(&description))) {
    m_adapterName = WideToUtf8(description.Description);
    m_dedicatedVideoMemoryBytes =
        static_cast<uint64_t>(description.DedicatedVideoMemory);
  }
}

bool GraphicsDevice::CreateRenderTargetView() {
  ComPtr<ID3D11Texture2D> backBuffer;
  HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
  if (FAILED(hr))
    return false;

  hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                        &m_renderTargetView);
  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice",
              "CreateRenderTargetView failed (hr={:08X}, removed={:08X})",
              static_cast<uint32_t>(hr),
              static_cast<uint32_t>(GetDeviceRemovedReason()));
  }
  return SUCCEEDED(hr);
}

bool GraphicsDevice::CreateDepthStencilView() {
  // 深度は3D描画パス専用のため、内部描画解像度(Render Scale適用後)・MSAAサンプル数
  // に合わせて作成する（出力解像度そのままではない点に注意）。
  const int samples = (std::max)(1, m_quality.msaaSamples);

  D3D11_TEXTURE2D_DESC depthDesc = {};
  depthDesc.Width = m_renderWidth;
  depthDesc.Height = m_renderHeight;
  depthDesc.MipLevels = 1;
  depthDesc.ArraySize = 1;
  depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthDesc.SampleDesc.Count = static_cast<UINT>(samples);
  UINT quality = 0;
  if (samples > 1) {
    m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT,
                                            depthDesc.SampleDesc.Count,
                                            &quality);
  }
  depthDesc.SampleDesc.Quality = (samples > 1 && quality > 0) ? quality - 1 : 0;
  depthDesc.Usage = D3D11_USAGE_DEFAULT;
  depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

  HRESULT hr =
      m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthStencilBuffer);
  if (FAILED(hr))
    return false;

  D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Format = depthDesc.Format;
  dsvDesc.ViewDimension = (samples > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS
                                       : D3D11_DSV_DIMENSION_TEXTURE2D;
  if (samples <= 1) {
    dsvDesc.Texture2D.MipSlice = 0;
  }

  hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc,
                                        &m_depthStencilView);
  return SUCCEEDED(hr);
}

bool GraphicsDevice::CreateSceneRenderTargets() {
  m_sceneColorTexMS.Reset();
  m_sceneColorRTVMS.Reset();
  m_sceneColorTexResolved.Reset();
  m_sceneColorRTVResolved.Reset();
  m_sceneColorSRVResolved.Reset();
  m_fxaaTex.Reset();
  m_fxaaRTV.Reset();
  m_fxaaSRV.Reset();
  m_depthStencilView.Reset();
  m_depthStencilBuffer.Reset();

  m_renderWidth = (std::max)(
      1u, static_cast<uint32_t>(static_cast<float>(m_width) * m_quality.renderScale));
  m_renderHeight = (std::max)(
      1u, static_cast<uint32_t>(static_cast<float>(m_height) * m_quality.renderScale));

  constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = m_renderWidth;
  desc.Height = m_renderHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = kSceneColorFormat;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_sceneColorTexResolved);
  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice",
             "CreateSceneRenderTargets: resolved color texture failed ({:08X})",
             static_cast<uint32_t>(hr));
    return false;
  }
  hr = m_device->CreateRenderTargetView(m_sceneColorTexResolved.Get(), nullptr,
                                        &m_sceneColorRTVResolved);
  if (FAILED(hr))
    return false;
  hr = m_device->CreateShaderResourceView(m_sceneColorTexResolved.Get(), nullptr,
                                          &m_sceneColorSRVResolved);
  if (FAILED(hr))
    return false;

  const int samples = (std::max)(1, m_quality.msaaSamples);
  if (samples > 1) {
    D3D11_TEXTURE2D_DESC msDesc = desc;
    msDesc.BindFlags = D3D11_BIND_RENDER_TARGET; // MSAAテクスチャは解決元専用（SRV不要）
    msDesc.SampleDesc.Count = static_cast<UINT>(samples);
    UINT quality = 0;
    m_device->CheckMultisampleQualityLevels(kSceneColorFormat,
                                            msDesc.SampleDesc.Count, &quality);
    msDesc.SampleDesc.Quality = (quality > 0) ? quality - 1 : 0;
    hr = m_device->CreateTexture2D(&msDesc, nullptr, &m_sceneColorTexMS);
    if (FAILED(hr)) {
      LOG_ERROR("GraphicsDevice",
               "CreateSceneRenderTargets: MSAA color texture failed ({:08X})",
               static_cast<uint32_t>(hr));
      return false;
    }
    hr = m_device->CreateRenderTargetView(m_sceneColorTexMS.Get(), nullptr,
                                          &m_sceneColorRTVMS);
    if (FAILED(hr))
      return false;
  }

  if (!CreateDepthStencilView())
    return false;

  if (m_quality.fxaaEnabled) {
    hr = m_device->CreateTexture2D(&desc, nullptr, &m_fxaaTex);
    if (FAILED(hr))
      return false;
    hr = m_device->CreateRenderTargetView(m_fxaaTex.Get(), nullptr, &m_fxaaRTV);
    if (FAILED(hr))
      return false;
    hr = m_device->CreateShaderResourceView(m_fxaaTex.Get(), nullptr, &m_fxaaSRV);
    if (FAILED(hr))
      return false;
  }

  return true;
}

bool GraphicsDevice::InitializePostProcessResources() {
  // フルスクリーン三角形（POSITION + TEXCOORD0、PostProcessVS.hlsl準拠）。
  // クリップ空間全体をはみ出して覆う「巨大三角形」でフルスクリーンクアッドを代用する。
  struct FullscreenVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
  };
  const FullscreenVertex vertices[] = {
      {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
      {{-1.0f, 3.0f, 0.0f}, {0.0f, -1.0f}},
      {{3.0f, -1.0f, 0.0f}, {2.0f, 1.0f}},
  };

  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.ByteWidth = sizeof(vertices);
  vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vbData = {};
  vbData.pSysMem = vertices;
  HRESULT hr = m_device->CreateBuffer(&vbDesc, &vbData, &m_fullscreenVB);
  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice", "InitializePostProcessResources: vertex buffer failed");
    return false;
  }

  const std::vector<D3D11_INPUT_ELEMENT_DESC> layout = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  if (!m_upscaleShader.LoadFromFile(m_device.Get(), L"Assets/shaders/PostProcessVS.hlsl",
                                    "main", L"Assets/shaders/UpscalePS.hlsl",
                                    "main", layout)) {
    LOG_ERROR("GraphicsDevice", "InitializePostProcessResources: Upscale shader failed");
    return false;
  }
  if (!m_fxaaShader.LoadFromFile(m_device.Get(), L"Assets/shaders/PostProcessVS.hlsl",
                                 "main", L"Assets/shaders/FXAAPS.hlsl", "main",
                                 layout)) {
    LOG_ERROR("GraphicsDevice", "InitializePostProcessResources: FXAA shader failed");
    return false;
  }

  D3D11_SAMPLER_DESC sampDesc = {};
  sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  hr = m_device->CreateSamplerState(&sampDesc, &m_linearSampler);
  if (FAILED(hr))
    return false;

  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = 16; // float4 1個分
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_fxaaConstantBuffer);
  if (FAILED(hr))
    return false;

  D3D11_BLEND_DESC blendDesc = {};
  blendDesc.RenderTarget[0].BlendEnable = FALSE;
  blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  hr = m_device->CreateBlendState(&blendDesc, &m_postProcessBlendState);
  if (FAILED(hr))
    return false;

  D3D11_DEPTH_STENCIL_DESC dsDesc = {};
  dsDesc.DepthEnable = FALSE;
  dsDesc.StencilEnable = FALSE;
  hr = m_device->CreateDepthStencilState(&dsDesc, &m_postProcessDepthState);
  if (FAILED(hr))
    return false;

  D3D11_RASTERIZER_DESC rastDesc = {};
  rastDesc.FillMode = D3D11_FILL_SOLID;
  rastDesc.CullMode = D3D11_CULL_NONE;
  rastDesc.DepthClipEnable = TRUE;
  hr = m_device->CreateRasterizerState(&rastDesc, &m_postProcessRasterizerState);
  if (FAILED(hr))
    return false;

  return true;
}

void GraphicsDevice::SetupSceneViewport() {
  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(m_renderWidth);
  viewport.Height = static_cast<float>(m_renderHeight);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  m_context->RSSetViewports(1, &viewport);
}

void GraphicsDevice::SetupBackbufferViewport() {
  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(m_width);
  viewport.Height = static_cast<float>(m_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  m_context->RSSetViewports(1, &viewport);
}

void GraphicsDevice::SetupRenderState() {
  // ラスタライザーステートの設定
  D3D11_RASTERIZER_DESC rasterDesc = {};
  rasterDesc.AntialiasedLineEnable = FALSE;
  rasterDesc.CullMode = D3D11_CULL_BACK; // 背面カリング有効（裏面を描画しない）
  rasterDesc.DepthBias = 0;
  rasterDesc.DepthBiasClamp = 0.0f;
  rasterDesc.DepthClipEnable = TRUE;
  rasterDesc.FillMode = D3D11_FILL_SOLID;
  rasterDesc.FrontCounterClockwise =
      FALSE; // DirectXTK/OBJ等は通常逆だが、CullNoneなら関係ない
  rasterDesc.MultisampleEnable = FALSE;
  rasterDesc.ScissorEnable = FALSE;
  rasterDesc.SlopeScaledDepthBias = 0.0f;

  m_device->CreateRasterizerState(&rasterDesc, &m_rasterizerState);
  m_context->RSSetState(m_rasterizerState.Get());

  // 深度ステンシルステートの設定
  D3D11_DEPTH_STENCIL_DESC depthDesc = {};
  depthDesc.DepthEnable = TRUE;
  depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
  depthDesc.StencilEnable = FALSE;

  m_device->CreateDepthStencilState(&depthDesc, &m_depthStencilState);
  m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
}

bool GraphicsDevice::InitializeGpuProfilerQueries() {
  D3D11_QUERY_DESC disjointDesc{};
  disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
  D3D11_QUERY_DESC pipelineDesc{};
  pipelineDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
  D3D11_QUERY_DESC timestampDesc{};
  timestampDesc.Query = D3D11_QUERY_TIMESTAMP;

  for (auto &frame : m_gpuQueryFrames) {
    if (FAILED(m_device->CreateQuery(&disjointDesc, &frame.disjoint)) ||
        FAILED(m_device->CreateQuery(&pipelineDesc, &frame.pipeline)) ||
        FAILED(m_device->CreateQuery(&timestampDesc, &frame.frameStart)) ||
        FAILED(m_device->CreateQuery(&timestampDesc, &frame.frameEnd))) {
      return false;
    }
  }
  return true;
}

void GraphicsDevice::ResolveGpuProfilerQueries() {
  if (!m_gpuProfilerAvailable) {
    return;
  }

  for (auto &frame : m_gpuQueryFrames) {
    if (!frame.issued) {
      continue;
    }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    const HRESULT disjointResult =
        m_context->GetData(frame.disjoint.Get(), &disjoint, sizeof(disjoint),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (disjointResult == S_FALSE) {
      continue;
    }

    core::GpuFrameSample sample;
    sample.frameIndex = frame.frameIndex;
    sample.valid = SUCCEEDED(disjointResult) && !disjoint.Disjoint &&
                   disjoint.Frequency != 0;

    uint64_t frameStart = 0;
    uint64_t frameEnd = 0;
    D3D11_QUERY_DATA_PIPELINE_STATISTICS pipeline{};
    if (sample.valid) {
      const HRESULT startResult =
          m_context->GetData(frame.frameStart.Get(), &frameStart,
                             sizeof(frameStart), D3D11_ASYNC_GETDATA_DONOTFLUSH);
      const HRESULT endResult =
          m_context->GetData(frame.frameEnd.Get(), &frameEnd, sizeof(frameEnd),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH);
      const HRESULT pipelineResult =
          m_context->GetData(frame.pipeline.Get(), &pipeline, sizeof(pipeline),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH);
      sample.valid = startResult == S_OK && endResult == S_OK &&
                     pipelineResult == S_OK && frameEnd >= frameStart;
    }

    if (sample.valid) {
      const double millisecondsPerTick =
          1000.0 / static_cast<double>(disjoint.Frequency);
      sample.scopes.push_back(
          {"GPU.Frame", static_cast<double>(frameEnd - frameStart) *
                            millisecondsPerTick});

      for (size_t i = 0; i < frame.usedScopeCount; ++i) {
        auto &scope = frame.scopes[i];
        uint64_t start = 0;
        uint64_t end = 0;
        const HRESULT startResult =
            m_context->GetData(scope.start.Get(), &start, sizeof(start),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const HRESULT endResult =
            m_context->GetData(scope.end.Get(), &end, sizeof(end),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (startResult == S_OK && endResult == S_OK && end >= start) {
          sample.scopes.push_back(
              {scope.name,
               static_cast<double>(end - start) * millisecondsPerTick});
        }
      }

      sample.pipeline.inputAssemblerVertices = pipeline.IAVertices;
      sample.pipeline.inputAssemblerPrimitives = pipeline.IAPrimitives;
      sample.pipeline.vertexShaderInvocations = pipeline.VSInvocations;
      sample.pipeline.pixelShaderInvocations = pipeline.PSInvocations;
    }

    m_readyGpuSamples.push_back(std::move(sample));
    frame.issued = false;
  }
}

} // namespace graphics
