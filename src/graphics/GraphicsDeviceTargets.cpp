/**
 * @file GraphicsDevice.cpp
 * @brief DirectX11デバイス・コンテキスト管理の実装
*/

#include "GraphicsDevice.h"
#include "../core/Logger.h"
#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <limits>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace graphics {

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
  m_depthSRV.Reset();

  // MSAA無効時のみ、ポストプロセスの霧計算用に深度をSRVとしても公開する
  // （typelessフォーマットでDSV/SRV両対応にする定石パターン）。MSAA有効時は
  // Texture2DMSの深度読み取りに追加実装が必要になるため、今回は霧を無効化する。
  const bool wantDepthSRV = (samples <= 1);

  D3D11_TEXTURE2D_DESC depthDesc = {};
  depthDesc.Width = m_renderWidth;
  depthDesc.Height = m_renderHeight;
  depthDesc.MipLevels = 1;
  depthDesc.ArraySize = 1;
  depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  if (wantDepthSRV) {
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  }
  depthDesc.SampleDesc.Count = static_cast<UINT>(samples);
  UINT quality = 0;
  if (samples > 1) {
    m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT,
                                            depthDesc.SampleDesc.Count,
                                            &quality);
  }
  depthDesc.SampleDesc.Quality = 0;
  if (samples > 1 && quality > 0) {
    depthDesc.SampleDesc.Quality = quality - 1;
  }
  depthDesc.Usage = D3D11_USAGE_DEFAULT;
  depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (wantDepthSRV) {
    depthDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
  }

  HRESULT hr =
      m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthStencilBuffer);
  if (FAILED(hr))
    return false;

  D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  if (samples > 1) {
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
  }
  if (samples <= 1) {
    dsvDesc.Texture2D.MipSlice = 0;
  }

  hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc,
                                        &m_depthStencilView);
  if (FAILED(hr))
    return false;

  if (wantDepthSRV) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &srvDesc,
                                            &m_depthSRV);
    if (FAILED(hr)) {
      LOG_WARN("GraphicsDevice",
               "CreateDepthStencilView: depth SRV failed ({:08X}); fog will be disabled",
               static_cast<uint32_t>(hr));
      m_depthSRV.Reset();
    }
  }

  return true;
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
  m_postProcessTex.Reset();
  m_postProcessRTV.Reset();
  m_postProcessSRV.Reset();
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
    msDesc.SampleDesc.Quality = 0;
    if (quality > 0) {
      msDesc.SampleDesc.Quality = quality - 1;
    }
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

  // ポストプロセス（霧/色調補正/ビネット/ブルーム）は常時適用するため常に作成する
  hr = m_device->CreateTexture2D(&desc, nullptr, &m_postProcessTex);
  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice",
             "CreateSceneRenderTargets: post-process texture failed ({:08X})",
             static_cast<uint32_t>(hr));
    return false;
  }
  hr = m_device->CreateRenderTargetView(m_postProcessTex.Get(), nullptr,
                                        &m_postProcessRTV);
  if (FAILED(hr))
    return false;
  hr = m_device->CreateShaderResourceView(m_postProcessTex.Get(), nullptr,
                                          &m_postProcessSRV);
  if (FAILED(hr))
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
  if (!m_postProcessShader.LoadFromFile(
          m_device.Get(), L"Assets/shaders/PostProcessVS.hlsl", "main",
          L"Assets/shaders/PostProcessPS.hlsl", "main", layout)) {
    LOG_ERROR("GraphicsDevice", "InitializePostProcessResources: PostProcess shader failed");
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
  hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_upscaleConstantBuffer);
  if (FAILED(hr))
    return false;

  D3D11_BUFFER_DESC ppCbDesc = {};
  ppCbDesc.ByteWidth = sizeof(PostProcessParams); // float4 x 7 = 112バイト
  ppCbDesc.Usage = D3D11_USAGE_DYNAMIC;
  ppCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  ppCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = m_device->CreateBuffer(&ppCbDesc, nullptr, &m_postProcessConstantBuffer);
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



} // namespace graphics
