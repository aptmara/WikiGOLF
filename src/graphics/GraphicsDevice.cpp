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

  if (!CreateSwapChainAndDevice(hWnd))
    return false;
  if (!CreateRenderTargetView())
    return false;
  if (!CreateDepthStencilView())
    return false;
  SetupViewport();

  m_gpuProfilerAvailable = InitializeGpuProfilerQueries();
  if (!m_gpuProfilerAvailable) {
    LOG_WARN("GraphicsDevice",
             "D3D11 GPU profiler queries are unavailable; CPU profiling will continue");
  }

  return true;
}

void GraphicsDevice::Shutdown() {
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

  float clearColor[4] = {r, g, b, a};
  m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
  m_context->ClearDepthStencilView(m_depthStencilView.Get(),
                                   D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                   1.0f, 0);
  m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(),
                                m_depthStencilView.Get());
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

  m_swapChain->Present(1, 0); // VSync有効
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
  m_depthStencilView.Reset();
  m_depthStencilBuffer.Reset();

  HRESULT hr =
      m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr))
    return false;

  if (!CreateRenderTargetView())
    return false;
  if (!CreateDepthStencilView())
    return false;
  SetupViewport();
  SetupRenderState();

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
  D3D11_TEXTURE2D_DESC depthDesc = {};
  depthDesc.Width = m_width;
  depthDesc.Height = m_height;
  depthDesc.MipLevels = 1;
  depthDesc.ArraySize = 1;
  depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthDesc.SampleDesc.Count = 1;
  depthDesc.SampleDesc.Quality = 0;
  depthDesc.Usage = D3D11_USAGE_DEFAULT;
  depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

  HRESULT hr =
      m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthStencilBuffer);
  if (FAILED(hr))
    return false;

  D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Format = depthDesc.Format;
  dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  dsvDesc.Texture2D.MipSlice = 0;

  hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc,
                                        &m_depthStencilView);
  return SUCCEEDED(hr);
}

void GraphicsDevice::SetupViewport() {
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
