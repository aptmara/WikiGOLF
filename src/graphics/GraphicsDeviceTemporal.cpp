/**
 * @file GraphicsDeviceTemporal.cpp
 * @brief テンポラルAA（TAA/TAAU・DLSS）の実装
 * @details
 *  - 3D描画は投影行列へサブピクセルジッターを加え、描画解像度で行う。
 *  - メッシュ描画中は SV_Target1 へ物体ごとの速度を書く（TemporalVelocity.hlsli）。
 *  - 解決時に「物体速度」と「深度から求めたカメラ速度」を合成した速度を作り、
 *    TAA（出力解像度の履歴へ蓄積）または DLSS で出力解像度へ復元する。
 */

#include "GraphicsDevice.h"
#include "../core/AntiAliasingMode.h"
#include "../core/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace graphics {

namespace {

/** @brief Halton列（サブピクセルジッター用の低食い違い列。DLSSの学習にも使われている）*/
float Halton(uint32_t index, uint32_t base) {
  float result = 0.0f;
  float fraction = 1.0f;
  while (index > 0) {
    fraction /= static_cast<float>(base);
    result += fraction * static_cast<float>(index % base);
    index /= base;
  }
  return result;
}

/**
 * @brief ジッターの周期。
 * @details 等倍で8、アップスケール時は画素面積比を掛ける
 *          （DLSS Programming Guide 3.7.1.1 の推奨式）。
 */
uint32_t JitterPhaseCount(uint32_t renderWidth, uint32_t outputWidth) {
  constexpr float kBasePhaseCount = 8.0f;
  if (renderWidth == 0) {
    return 8;
  }
  const float ratio = static_cast<float>(outputWidth) / static_cast<float>(renderWidth);
  const float phases = kBasePhaseCount * ratio * ratio;
  return static_cast<uint32_t>(std::clamp(std::ceil(phases), 8.0f, 72.0f));
}

DlssUpscaler::Quality ToDlssQuality(float renderScale) {
  switch (core::DlssQualityFromRenderScale(renderScale)) {
  case core::DlssQuality::Performance:
    return DlssUpscaler::Quality::Performance;
  case core::DlssQuality::Balanced:
    return DlssUpscaler::Quality::Balanced;
  case core::DlssQuality::Dlaa:
    return DlssUpscaler::Quality::Dlaa;
  case core::DlssQuality::Quality:
  default:
    return DlssUpscaler::Quality::Quality;
  }
}

} // namespace

void GraphicsDevice::DecideTemporalModeAndRenderSize() {
  m_temporalMode = TemporalMode::None;
  m_renderWidth = (std::max)(
      1u, static_cast<uint32_t>(static_cast<float>(m_width) * m_quality.renderScale));
  m_renderHeight = (std::max)(
      1u, static_cast<uint32_t>(static_cast<float>(m_height) * m_quality.renderScale));

  // 速度・深度をシェーダーから読むため、MSAAとは併用しない
  if (m_quality.msaaSamples > 1 || !m_velocityResolveShader.IsValid()) {
    return;
  }

  if (m_quality.dlssEnabled && m_dlss.IsSupported()) {
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    if (m_dlss.QueryRenderSize(m_width, m_height,
                               ToDlssQuality(m_quality.renderScale), renderWidth,
                               renderHeight)) {
      m_renderWidth = (std::max)(1u, renderWidth);
      m_renderHeight = (std::max)(1u, renderHeight);
      m_temporalMode = TemporalMode::Dlss;
      return;
    }
    LOG_WARN("GraphicsDevice", "DLSS optimal settings query failed; falling back to TAA");
  }

  // DLSSを選んだが使えない環境では、同じ入力で動くTAAで代替する
  if ((m_quality.taaEnabled || m_quality.dlssEnabled) && m_taaShader.IsValid()) {
    m_temporalMode = TemporalMode::Taa;
  }
}

bool GraphicsDevice::CreateTemporalTargets(const D3D11_TEXTURE2D_DESC &sceneColorDesc) {
  if (!m_depthSRV) {
    return false;
  }

  auto createTarget = [&](const D3D11_TEXTURE2D_DESC &desc,
                          ComPtr<ID3D11Texture2D> &texture,
                          ComPtr<ID3D11RenderTargetView> *rtv,
                          ComPtr<ID3D11ShaderResourceView> &srv) {
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &texture))) {
      return false;
    }
    if (rtv && FAILED(m_device->CreateRenderTargetView(texture.Get(), nullptr,
                                                       rtv->ReleaseAndGetAddressOf()))) {
      return false;
    }
    return SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(), nullptr,
                                                        &srv));
  };

  D3D11_TEXTURE2D_DESC velocityDesc = sceneColorDesc;
  velocityDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  if (!createTarget(velocityDesc, m_velocityTex, &m_velocityRTV, m_velocitySRV)) {
    return false;
  }

  D3D11_TEXTURE2D_DESC resolvedDesc = sceneColorDesc;
  resolvedDesc.Format = DXGI_FORMAT_R16G16_FLOAT; // DLSSの入力形式に合わせる
  if (!createTarget(resolvedDesc, m_resolvedVelocityTex, &m_resolvedVelocityRTV,
                    m_resolvedVelocitySRV)) {
    return false;
  }

  D3D11_TEXTURE2D_DESC outputDesc = sceneColorDesc;
  outputDesc.Width = m_width;
  outputDesc.Height = m_height;

  if (m_temporalMode == TemporalMode::Taa) {
    for (size_t i = 0; i < m_taaHistoryTex.size(); ++i) {
      if (!createTarget(outputDesc, m_taaHistoryTex[i], &m_taaHistoryRTV[i],
                        m_taaHistorySRV[i])) {
        return false;
      }
    }
  } else if (m_temporalMode == TemporalMode::Dlss) {
    D3D11_TEXTURE2D_DESC dlssDesc = outputDesc;
    // DLSSは出力へコンピュートシェーダーで書き込むためUAVが必要
    dlssDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (!createTarget(dlssDesc, m_dlssOutputTex, nullptr, m_dlssOutputSRV)) {
      return false;
    }
  }

  m_temporalHistoryValid = false;
  return true;
}

void GraphicsDevice::ReleaseTemporalTargets() {
  m_velocityTex.Reset();
  m_velocityRTV.Reset();
  m_velocitySRV.Reset();
  m_resolvedVelocityTex.Reset();
  m_resolvedVelocityRTV.Reset();
  m_resolvedVelocitySRV.Reset();
  for (size_t i = 0; i < m_taaHistoryTex.size(); ++i) {
    m_taaHistoryTex[i].Reset();
    m_taaHistoryRTV[i].Reset();
    m_taaHistorySRV[i].Reset();
  }
  m_dlssOutputTex.Reset();
  m_dlssOutputSRV.Reset();
  m_dlss.ReleaseFeature();
  m_temporalHistoryValid = false;
}

void GraphicsDevice::BeginTemporalFrame() {
  // 前フレームに登録されたカメラ行列を「前フレーム」として繰り越す
  m_temporalPrevViewProjectionValid = m_temporalCameraSetThisFrame;
  if (m_temporalCameraSetThisFrame) {
    m_temporalPrevViewProjection = m_temporalViewProjection;
  }
  m_temporalCameraSetThisFrame = false;

  const auto now = std::chrono::steady_clock::now();
  if (m_lastTemporalFrameAt.time_since_epoch().count() != 0) {
    m_temporalFrameTimeMs =
        std::chrono::duration<float, std::milli>(now - m_lastTemporalFrameAt).count();
  }
  m_lastTemporalFrameAt = now;

  m_jitterNdc = {0.0f, 0.0f};
  if (!IsTemporalActive() || m_renderWidth == 0 || m_renderHeight == 0) {
    return;
  }

  const uint32_t phaseCount = JitterPhaseCount(m_renderWidth, m_width);
  m_temporalFrameIndex = (m_temporalFrameIndex + 1) % phaseCount;
  const uint32_t sample = m_temporalFrameIndex + 1; // Halton(0)=0を避ける
  const float jitterX = Halton(sample, 2) - 0.5f;   // 描画解像度のピクセル単位（右が正）
  const float jitterY = Halton(sample, 3) - 0.5f;   // 描画解像度のピクセル単位（下が正）
  m_jitterNdc = {2.0f * jitterX / static_cast<float>(m_renderWidth),
                 -2.0f * jitterY / static_cast<float>(m_renderHeight)};

  if (m_velocityRTV) {
    // 書き込まれなかった画素（スカイボックス等）は「カメラ移動のみ」として扱う
    const float clearVelocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_context->ClearRenderTargetView(m_velocityRTV.Get(), clearVelocity);
  }
}

void GraphicsDevice::ApplyProjectionJitter(DirectX::XMMATRIX &projection) const {
  if (m_jitterNdc.x == 0.0f && m_jitterNdc.y == 0.0f) {
    return;
  }
  // 行ベクトル規約では clip.xy += view.z * m[2].xy、clip.w = view.z となるため、
  // 3行目へ加算するとNDC上で奥行きに依らず一定量だけずれる。
  projection.r[2] = DirectX::XMVectorAdd(
      projection.r[2],
      DirectX::XMVectorSet(m_jitterNdc.x, m_jitterNdc.y, 0.0f, 0.0f));
}

void GraphicsDevice::SetTemporalCameraViewProjection(
    const DirectX::XMMATRIX &viewProjection) {
  DirectX::XMStoreFloat4x4(&m_temporalViewProjection, viewProjection);
  m_temporalCameraSetThisFrame = true;
}

GraphicsDevice::TemporalShaderConstants
GraphicsDevice::GetTemporalShaderConstants() const {
  TemporalShaderConstants constants;
  // 前フレームのカメラが無い（初回・シーン切替直後）なら今フレームで代用し、
  // 物体の動きだけを速度として扱う
  constants.prevViewProjection = DirectX::XMLoadFloat4x4(
      m_temporalPrevViewProjectionValid ? &m_temporalPrevViewProjection
                                        : &m_temporalViewProjection);
  constants.jitterUv = {m_jitterNdc.x * 0.5f, -m_jitterNdc.y * 0.5f,
                        m_renderWidth > 0 ? 1.0f / static_cast<float>(m_renderWidth) : 0.0f,
                        m_renderHeight > 0 ? 1.0f / static_cast<float>(m_renderHeight) : 0.0f};
  constants.velocityEnabled = IsTemporalActive() && m_velocityRTV != nullptr;
  return constants;
}

void GraphicsDevice::BindSceneVelocityTarget() {
  if (!IsTemporalActive() || !m_velocityRTV || !m_sceneColorRTVResolved) {
    return;
  }
  ID3D11RenderTargetView *targets[2] = {m_sceneColorRTVResolved.Get(),
                                        m_velocityRTV.Get()};
  m_context->OMSetRenderTargets(2, targets, m_depthStencilView.Get());
}

void GraphicsDevice::UnbindSceneVelocityTarget() {
  if (!IsTemporalActive() || !m_velocityRTV || !m_sceneColorRTVResolved) {
    return;
  }
  ID3D11RenderTargetView *target = m_sceneColorRTVResolved.Get();
  m_context->OMSetRenderTargets(1, &target, m_depthStencilView.Get());
}

float GraphicsDevice::GetTextureMipBias() const {
  if (!IsTemporalActive() || m_width == 0 || m_renderWidth >= m_width) {
    return 0.0f;
  }
  return std::log2(static_cast<float>(m_renderWidth) / static_cast<float>(m_width));
}

void GraphicsDevice::DrawFullscreenTriangle(Shader &shader,
                                            ID3D11RenderTargetView *dstRTV,
                                            uint32_t dstWidth, uint32_t dstHeight) {
  D3D11_VIEWPORT vp = {};
  vp.Width = static_cast<float>(dstWidth);
  vp.Height = static_cast<float>(dstHeight);
  vp.MaxDepth = 1.0f;
  m_context->RSSetViewports(1, &vp);
  m_context->OMSetRenderTargets(1, &dstRTV, nullptr);

  shader.Bind(m_context.Get());
  UINT stride = sizeof(DirectX::XMFLOAT3) + sizeof(DirectX::XMFLOAT2);
  UINT offset = 0;
  m_context->IASetVertexBuffers(0, 1, m_fullscreenVB.GetAddressOf(), &stride,
                                &offset);
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->RSSetState(m_postProcessRasterizerState.Get());
  m_context->OMSetBlendState(m_postProcessBlendState.Get(), nullptr, 0xFFFFFFFF);
  m_context->OMSetDepthStencilState(m_postProcessDepthState.Get(), 0);
  m_context->Draw(3, 0);
}

void GraphicsDevice::RunVelocityResolvePass() {
  if (!m_resolvedVelocityRTV || !m_velocitySRV || !m_depthSRV ||
      !m_velocityResolveShader.IsValid()) {
    return;
  }

  // 前フレームのカメラが無ければ、カメラ由来の速度は0とする
  const bool prevValid =
      m_temporalCameraSetThisFrame && m_temporalPrevViewProjectionValid;

  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(m_context->Map(m_velocityResolveConstantBuffer.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    struct VelocityResolveConstants {
      DirectX::XMFLOAT4X4 invViewProjection;
      DirectX::XMFLOAT4X4 prevViewProjection;
      DirectX::XMFLOAT4 jitterUv;
      DirectX::XMFLOAT4 params;
    } constants{};
    const DirectX::XMMATRIX viewProjection =
        DirectX::XMLoadFloat4x4(&m_temporalViewProjection);
    DirectX::XMVECTOR determinant;
    // HLSLは列優先で読むため転置して渡す（シェーダー側は mul(v, M) の行ベクトル規約）
    DirectX::XMStoreFloat4x4(
        &constants.invViewProjection,
        DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(&determinant, viewProjection)));
    DirectX::XMStoreFloat4x4(
        &constants.prevViewProjection,
        DirectX::XMMatrixTranspose(
            DirectX::XMLoadFloat4x4(&m_temporalPrevViewProjection)));
    const TemporalShaderConstants temporal = GetTemporalShaderConstants();
    constants.jitterUv = temporal.jitterUv;
    constants.params = {prevValid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    m_context->Unmap(m_velocityResolveConstantBuffer.Get(), 0);
  }

  ID3D11ShaderResourceView *srvs[2] = {m_velocitySRV.Get(), m_depthSRV.Get()};
  m_context->PSSetShaderResources(0, 2, srvs);
  m_context->PSSetConstantBuffers(0, 1, m_velocityResolveConstantBuffer.GetAddressOf());
  DrawFullscreenTriangle(m_velocityResolveShader, m_resolvedVelocityRTV.Get(),
                         m_renderWidth, m_renderHeight);
  ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
  m_context->PSSetShaderResources(0, 2, nullSRVs);
}

ID3D11ShaderResourceView *GraphicsDevice::RunTaaPass() {
  if (!m_taaShader.IsValid() || !m_taaHistoryRTV[0] || !m_taaHistoryRTV[1] ||
      !m_resolvedVelocitySRV) {
    return nullptr;
  }

  const size_t writeIndex = m_taaWriteIndex;
  const size_t readIndex = 1 - writeIndex;
  // カメラ未登録のフレーム（3Dカメラが無いシーン等）は再投影できないため、
  // 現フレームをそのまま履歴へ書き、次フレームから蓄積をやり直す。
  const bool useHistory = m_temporalHistoryValid && m_temporalCameraSetThisFrame;

  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(m_context->Map(m_taaConstantBuffer.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    struct TaaConstants {
      DirectX::XMFLOAT4 renderSize; // w, h, 1/w, 1/h
      DirectX::XMFLOAT4 outputSize; // w, h, 1/w, 1/h
      DirectX::XMFLOAT4 jitterUv;   // xy: ジッター(UV), z: 履歴を使うか(1/0)
      DirectX::XMFLOAT4 params;     // 未使用
    } constants{};
    const float renderWidth = static_cast<float>(m_renderWidth);
    const float renderHeight = static_cast<float>(m_renderHeight);
    const float outputWidth = static_cast<float>(m_width);
    const float outputHeight = static_cast<float>(m_height);
    constants.renderSize = {renderWidth, renderHeight, 1.0f / renderWidth,
                            1.0f / renderHeight};
    constants.outputSize = {outputWidth, outputHeight, 1.0f / outputWidth,
                            1.0f / outputHeight};
    constants.jitterUv = {m_jitterNdc.x * 0.5f, -m_jitterNdc.y * 0.5f,
                          useHistory ? 1.0f : 0.0f, 0.0f};
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    m_context->Unmap(m_taaConstantBuffer.Get(), 0);
  }

  ID3D11ShaderResourceView *srvs[4] = {m_sceneColorSRVResolved.Get(),
                                       m_resolvedVelocitySRV.Get(), m_depthSRV.Get(),
                                       m_taaHistorySRV[readIndex].Get()};
  m_context->PSSetShaderResources(0, 4, srvs);
  ID3D11SamplerState *samplers[2] = {m_linearSampler.Get(), m_pointSampler.Get()};
  m_context->PSSetSamplers(0, 2, samplers);
  m_context->PSSetConstantBuffers(0, 1, m_taaConstantBuffer.GetAddressOf());

  DrawFullscreenTriangle(m_taaShader, m_taaHistoryRTV[writeIndex].Get(), m_width,
                         m_height);

  ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
  m_context->PSSetShaderResources(0, 4, nullSRVs);

  m_taaWriteIndex = readIndex;
  m_temporalHistoryValid = m_temporalCameraSetThisFrame;
  return m_taaHistorySRV[writeIndex].Get();
}

ID3D11ShaderResourceView *GraphicsDevice::RunDlssPass() {
  if (!m_dlssOutputTex || !m_resolvedVelocityTex || !m_depthStencilBuffer) {
    return nullptr;
  }
  // 直前の速度解決パスの出力先（解決済み速度）を外し、DLSSが入力として読めるようにする
  m_context->OMSetRenderTargets(0, nullptr, nullptr);

  DlssUpscaler::EvaluateParams params;
  params.color = m_sceneColorTexResolved.Get();
  params.depth = m_depthStencilBuffer.Get();
  params.motionVectors = m_resolvedVelocityTex.Get();
  params.output = m_dlssOutputTex.Get();
  params.renderWidth = m_renderWidth;
  params.renderHeight = m_renderHeight;
  params.outputWidth = m_width;
  params.outputHeight = m_height;
  params.quality = ToDlssQuality(m_quality.renderScale);
  // 投影行列へ加えたジッターを描画解像度ピクセル単位で渡す（右・下が正）
  params.jitterOffsetX = m_jitterNdc.x * 0.5f * static_cast<float>(m_renderWidth);
  params.jitterOffsetY = -m_jitterNdc.y * 0.5f * static_cast<float>(m_renderHeight);
  // 速度は「今のUV − 前のUV」なので、前フレームへ向かうピクセル量へ符号反転して換算する
  params.motionVectorScaleX = -static_cast<float>(m_renderWidth);
  params.motionVectorScaleY = -static_cast<float>(m_renderHeight);
  params.frameTimeMs = m_temporalFrameTimeMs;
  params.reset = !(m_temporalHistoryValid && m_temporalCameraSetThisFrame);

  if (!m_dlss.Evaluate(m_context.Get(), params)) {
    m_temporalHistoryValid = false;
    return nullptr;
  }
  m_temporalHistoryValid = m_temporalCameraSetThisFrame;
  return m_dlssOutputSRV.Get();
}

} // namespace graphics
