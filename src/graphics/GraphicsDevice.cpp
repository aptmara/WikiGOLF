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

bool GraphicsDevice::Initialize(HWND hWnd, uint32_t width, uint32_t height,
                                const std::wstring &preferredAdapterName) {
  m_width = width;
  m_height = height;
  m_renderWidth = width;
  m_renderHeight = height;
  m_preferredAdapterName = preferredAdapterName;

  if (!CreateSwapChainAndDevice(hWnd))
    return false;
  if (!CreateRenderTargetView())
    return false;
  if (!InitializePostProcessResources())
    return false;
  if (!CreateSceneRenderTargets())
    return false;
  SetupSceneViewport();

#ifdef WIKIGOLF_PROFILING
  m_gpuProfilerAvailable = InitializeGpuProfilerQueries();
  if (!m_gpuProfilerAvailable) {
    LOG_WARN("GraphicsDevice",
             "D3D11 GPU profiler queries are unavailable; CPU profiling will continue");
  }
#endif

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
  m_depthSRV.Reset();
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
  m_fullscreenVB.Reset();
  m_linearSampler.Reset();
  m_fxaaConstantBuffer.Reset();
  m_upscaleConstantBuffer.Reset();
  m_postProcessConstantBuffer.Reset();
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
#ifdef WIKIGOLF_PROFILING
  ResolveGpuProfilerQueries();
#endif
  m_gpuScopeStack.clear();
  m_currentGpuQueryFrame = nullptr;

#ifdef WIKIGOLF_PROFILING
  if (m_gpuProfilerAvailable) {
    auto &queryFrame = m_gpuQueryFrames[m_gpuQueryWriteIndex];
    if (!queryFrame.issued) {
      queryFrame.frameIndex = profileFrameIndex;
      queryFrame.usedScopeCount = 0;
      queryFrame.pipelineIssued =
          (profileFrameIndex % kPipelineStatisticsInterval) == 0;
      m_context->Begin(queryFrame.disjoint.Get());
      m_context->End(queryFrame.frameStart.Get());
      if (queryFrame.pipelineIssued) {
        m_context->Begin(queryFrame.pipeline.Get());
      }
      m_currentGpuQueryFrame = &queryFrame;
    }
  }
#endif

  // 3D描画（Skybox/メッシュ）は内部描画解像度のオフスクリーンターゲットへ描画する。
  // MSAA有効時はMSAAカラーターゲットへ、無効時は直接「解決済み」ターゲットへ描画する。
  ID3D11RenderTargetView *sceneRTV = m_sceneColorRTVResolved.Get();
  if (m_quality.msaaSamples > 1) {
    sceneRTV = m_sceneColorRTVMS.Get();
  }

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
                                       FullscreenConstants constants) {
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

  if (constants == FullscreenConstants::Fxaa) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_fxaaConstantBuffer.Get(), 0,
                                 D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      float *data = static_cast<float *>(mapped.pData);
      data[0] = 0.0f;
      data[1] = 0.0f;
      if (m_renderWidth > 0) {
        data[0] = 1.0f / static_cast<float>(m_renderWidth);
      }
      if (m_renderHeight > 0) {
        data[1] = 1.0f / static_cast<float>(m_renderHeight);
      }
      data[2] = 0.0f;
      data[3] = 0.0f;
      m_context->Unmap(m_fxaaConstantBuffer.Get(), 0);
    }
    m_context->PSSetConstantBuffers(0, 1, m_fxaaConstantBuffer.GetAddressOf());
  } else if (constants == FullscreenConstants::Upscale) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_upscaleConstantBuffer.Get(), 0,
                                 D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      float *data = static_cast<float *>(mapped.pData);
      data[0] = 0.0f;
      data[1] = 0.0f;
      if (m_renderWidth > 0) {
        data[0] = 1.0f / static_cast<float>(m_renderWidth);
      }
      if (m_renderHeight > 0) {
        data[1] = 1.0f / static_cast<float>(m_renderHeight);
      }
      // Render Scaleで縮小しているほどアップスケールのぼやけが目立つため、
      // 縮小率に応じてシャープ量を自動調整する（CAS的な軽量アンシャープマスク）。
      const float downscale = 1.0f - m_quality.renderScale;
      data[2] = std::clamp(downscale * 1.5f, 0.0f, 0.6f);
      data[3] = 0.0f;
      m_context->Unmap(m_upscaleConstantBuffer.Get(), 0);
    }
    m_context->PSSetConstantBuffers(0, 1, m_upscaleConstantBuffer.GetAddressOf());
  }

  m_context->RSSetState(m_postProcessRasterizerState.Get());
  m_context->OMSetBlendState(m_postProcessBlendState.Get(), nullptr, 0xFFFFFFFF);
  m_context->OMSetDepthStencilState(m_postProcessDepthState.Get(), 0);

  m_context->Draw(3, 0);

  ID3D11ShaderResourceView *nullSRV = nullptr;
  m_context->PSSetShaderResources(0, 1, &nullSRV);
}

void GraphicsDevice::RunPostProcessPass(ID3D11ShaderResourceView *colorSRV,
                                        ID3D11RenderTargetView *dstRTV) {
  if (!m_postProcessShader.IsValid() || !colorSRV || !dstRTV) {
    return;
  }

  D3D11_VIEWPORT vp = {};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(m_renderWidth);
  vp.Height = static_cast<float>(m_renderHeight);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  m_context->RSSetViewports(1, &vp);
  m_context->OMSetRenderTargets(1, &dstRTV, nullptr);

  m_postProcessShader.Bind(m_context.Get());

  UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT2);
  UINT offset = 0;
  m_context->IASetVertexBuffers(0, 1, m_fullscreenVB.GetAddressOf(), &stride,
                                &offset);
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  ID3D11ShaderResourceView *srvs[2] = {colorSRV, m_depthSRV.Get()};
  m_context->PSSetShaderResources(0, 2, srvs);
  m_context->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(m_context->Map(m_postProcessConstantBuffer.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    PostProcessParams params = m_postProcessParams;
    // 深度が読めない（MSAA有効）場合は霧を強制的に無効化する
    params.depthParams.z = 0.0f;
    if (IsDepthReadable()) {
      params.depthParams.z = 1.0f;
    }
    std::memcpy(mapped.pData, &params, sizeof(PostProcessParams));
    m_context->Unmap(m_postProcessConstantBuffer.Get(), 0);
  }
  m_context->PSSetConstantBuffers(0, 1, m_postProcessConstantBuffer.GetAddressOf());

  m_context->RSSetState(m_postProcessRasterizerState.Get());
  m_context->OMSetBlendState(m_postProcessBlendState.Get(), nullptr, 0xFFFFFFFF);
  m_context->OMSetDepthStencilState(m_postProcessDepthState.Get(), 0);

  m_context->Draw(3, 0);

  ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
  m_context->PSSetShaderResources(0, 2, nullSRVs);
}

void GraphicsDevice::ResolveSceneToBackbuffer() {
  if (m_quality.msaaSamples > 1 && m_sceneColorTexMS && m_sceneColorTexResolved) {
    m_context->ResolveSubresource(m_sceneColorTexResolved.Get(), 0,
                                  m_sceneColorTexMS.Get(), 0,
                                  DXGI_FORMAT_R8G8B8A8_UNORM);
  }

  ID3D11ShaderResourceView *sourceSRV = m_sceneColorSRVResolved.Get();

  // 霧/色調補正/ビネット/ブルームは常時適用する
  RunPostProcessPass(sourceSRV, m_postProcessRTV.Get());
  sourceSRV = m_postProcessSRV.Get();

  if (m_quality.fxaaEnabled && m_fxaaRTV && m_fxaaSRV) {
    RunFullscreenPass(m_fxaaShader, sourceSRV, m_fxaaRTV.Get(), m_renderWidth,
                      m_renderHeight, FullscreenConstants::Fxaa);
    sourceSRV = m_fxaaSRV.Get();
  }

  // 内部描画解像度 → 出力(バックバッファ)解像度へアップスケール（+自動シャープ化）
  RunFullscreenPass(m_upscaleShader, sourceSRV, m_renderTargetView.Get(),
                    m_width, m_height, FullscreenConstants::Upscale);

  // 以降のUI(D2D)/ScreenFadeはバックバッファへ直接描画されるため、
  // 状態を出力解像度基準に戻しておく（深度テストは既定の有効状態へ復帰）。
  m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
  m_context->RSSetState(nullptr);
  m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
  m_context->OMSetDepthStencilState(nullptr, 0);
  SetupBackbufferViewport();
}

void GraphicsDevice::EndFrame() {
#ifdef WIKIGOLF_PROFILING
  if (m_currentGpuQueryFrame) {
    while (!m_gpuScopeStack.empty()) {
      EndGpuScope();
    }
    if (m_currentGpuQueryFrame->pipelineIssued) {
      m_context->End(m_currentGpuQueryFrame->pipeline.Get());
    }
    m_context->End(m_currentGpuQueryFrame->frameEnd.Get());
    m_context->End(m_currentGpuQueryFrame->disjoint.Get());
    m_currentGpuQueryFrame->issued = true;
    m_gpuQueryWriteIndex = (m_gpuQueryWriteIndex + 1) % kGpuQueryBufferCount;
    m_currentGpuQueryFrame = nullptr;
  }
#endif

  UINT syncInterval = 0;
  if (m_vsyncEnabled) {
    syncInterval = 1;
  }
  m_swapChain->Present(syncInterval, 0);
#ifdef WIKIGOLF_PROFILING
  ResolveGpuProfilerQueries();
#endif
}

void GraphicsDevice::BeginGpuScope(std::string_view name) {
#ifdef WIKIGOLF_PROFILING
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
#else
  (void)name;
#endif
}

void GraphicsDevice::EndGpuScope() {
#ifdef WIKIGOLF_PROFILING
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
#endif
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



} // namespace graphics
