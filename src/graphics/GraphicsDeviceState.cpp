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
