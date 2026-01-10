/**
 * @file GraphicsDevice.cpp
 * @brief DirectX11デバイス・コンテキスト管理の実装
 */

#include "GraphicsDevice.h"
#include "../core/Logger.h"
#include <d3d11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace graphics {

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

  return true;
}

void GraphicsDevice::Shutdown() {
  if (m_context) {
    m_context->ClearState();
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

void GraphicsDevice::BeginFrame(float r, float g, float b, float a) {
  float clearColor[4] = {r, g, b, a};
  m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
  m_context->ClearDepthStencilView(m_depthStencilView.Get(),
                                   D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                   1.0f, 0);
  m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(),
                                m_depthStencilView.Get());
}

void GraphicsDevice::EndFrame() {
  m_swapChain->Present(1, 0); // VSync有効
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

  D3D_FEATURE_LEVEL featureLevel;
  auto tryCreate = [&](D3D_DRIVER_TYPE type) {
    return D3D11CreateDeviceAndSwapChain(
        nullptr, type, nullptr, createDeviceFlags, featureLevels,
        _countof(featureLevels), D3D11_SDK_VERSION, &swapChainDesc,
        &m_swapChain, &m_device, &featureLevel, &m_context);
  };

  // WARPを先に試し、ハードウェアが安定しない環境でも起動を優先
  HRESULT hr = tryCreate(D3D_DRIVER_TYPE_WARP);
  m_driverType = D3D_DRIVER_TYPE_WARP;
  if (FAILED(hr)) {
    LOG_WARN("GraphicsDevice",
             "WARP device creation failed (hr={:08X}), trying hardware",
             static_cast<uint32_t>(hr));
    hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE);
    m_driverType = D3D_DRIVER_TYPE_HARDWARE;
  }

  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice", "Device creation failed (hr={:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  m_featureLevel = featureLevel;
  LOG_INFO("GraphicsDevice", "Device created. Driver={}, FeatureLevel=0x{:04X}",
           driverTypeToStr(m_driverType),
           static_cast<uint32_t>(m_featureLevel));

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
  // 1. Rasterizer State (カリングなし)
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

  // 2. Depth Stencil State (深度テスト有効/書き込み有効)
  D3D11_DEPTH_STENCIL_DESC depthDesc = {};
  depthDesc.DepthEnable = TRUE;
  depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
  depthDesc.StencilEnable = FALSE;

  m_device->CreateDepthStencilState(&depthDesc, &m_depthStencilState);
  m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
}

} // namespace graphics
