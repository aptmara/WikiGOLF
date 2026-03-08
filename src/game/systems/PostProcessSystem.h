/**
 * @file PostProcessSystem.h
 * @brief ポストプロセスエフェクトシステム - 霧、色調補正、ビネット
 */

#pragma once

#include "../../core/GameContext.h"
#include "../components/EnvironmentState.h"
#include <algorithm>
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace game::systems {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

/**
 * @brief ポストプロセス定数バッファ
 */
struct PostProcessConstants {
  // 霧
  XMFLOAT4 fogColor;  // RGB + density
  XMFLOAT4 fogParams; // start, end, 0, 0

  // 色調補正
  XMFLOAT4 colorTint;   // RGB tint + brightness
  XMFLOAT4 colorParams; // saturation, contrast, 0, 0

  // ビネット
  XMFLOAT4 vignetteParams; // intensity, radius, softness, 0

  // 時間・その他
  XMFLOAT4 timeParams; // time, 0, 0, 0
};

/**
 * @brief ポストプロセスシステム
 */
class PostProcessSystem {
public:
  /**
   * @brief 初期化
   */
  bool Initialize(ID3D11Device *device) {
    m_device = device;

    // 定数バッファ作成
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(PostProcessConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr))
      return false;

    // フルスクリーン三角形用頂点バッファ
    struct FullscreenVertex {
      XMFLOAT3 pos;
      XMFLOAT2 uv;
    };

    // 巨大な三角形で画面全体をカバー
    FullscreenVertex vertices[] = {
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

    hr = device->CreateBuffer(&vbDesc, &vbData, &m_vertexBuffer);
    if (FAILED(hr))
      return false;

    // サンプラーステート
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = device->CreateSamplerState(&sampDesc, &m_samplerState);
    if (FAILED(hr))
      return false;

    // ブレンドステート（通常）
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateBlendState(&blendDesc, &m_blendState);
    if (FAILED(hr))
      return false;

    m_initialized = true;
    return true;
  }

  /**
   * @brief 環境状態から定数を更新
   */
  void UpdateFromEnvironment(const components::EnvironmentState &env,
                             float time) {
    using LightingMood = game::components::LightingMood;

    constexpr float kMinBrightnessForReadability = 0.85f;
    float brightness =
        std::max(env.brightness, kMinBrightnessForReadability); // 暗いテーマでの視認性確保
    bool brightnessClamped = brightness > env.brightness;

    m_constants.fogColor = {env.fogColor.x, env.fogColor.y, env.fogColor.z,
                            env.fogDensity};
    m_constants.fogParams = {env.fogStart, env.fogEnd, 0, 0};

    m_constants.colorTint = {env.colorTint.x, env.colorTint.y, env.colorTint.z,
                             brightness};
    m_constants.colorParams = {env.saturation, env.contrast, 0, 0};

    // ビネットはライティングムードに応じて調整
    float vignetteIntensity = 0.3f;

    int currentMood = static_cast<int>(env.lightingMood);

    // Horrorは定義されていないため削除、StormyDarknessのみ
    if (currentMood == static_cast<int>(LightingMood::StormyDarkness)) {
      vignetteIntensity = 0.6f;
    } else if (currentMood ==
                   static_cast<int>(LightingMood::CandlelitAntique) ||
               currentMood == static_cast<int>(LightingMood::MoonlitNight)) {
      vignetteIntensity = 0.4f;
    } else if (currentMood == static_cast<int>(LightingMood::StarlitCosmos) ||
               currentMood == static_cast<int>(LightingMood::NeonCyberpunk)) {
      vignetteIntensity = 0.2f;
    } else {
      vignetteIntensity = 0.25f;
    }
    if (brightnessClamped) {
      vignetteIntensity = std::max(0.2f, vignetteIntensity * 0.7f);
    }
    m_constants.vignetteParams = {vignetteIntensity, 0.7f, 0.5f, 0};
    m_constants.timeParams = {time, 0, 0, 0};
  }

  /**
   * @brief デフォルト設定でリセット
   */
  void ResetToDefaults() {
    m_constants.fogColor = {0.7f, 0.75f, 0.8f, 0.0f};
    m_constants.fogParams = {100.0f, 500.0f, 0, 0};
    m_constants.colorTint = {1.0f, 1.0f, 1.0f, 1.0f};
    m_constants.colorParams = {1.0f, 1.0f, 0, 0};
    m_constants.vignetteParams = {0.25f, 0.7f, 0.5f, 0};
    m_constants.timeParams = {0, 0, 0, 0};
  }

  /**
   * @brief 霧パラメータ設定
   */
  void SetFog(const XMFLOAT3 &color, float density, float start, float end) {
    m_constants.fogColor = {color.x, color.y, color.z, density};
    m_constants.fogParams = {start, end, 0, 0};
  }

  /**
   * @brief 色調補正設定
   */
  void SetColorGrading(const XMFLOAT3 &tint, float brightness, float saturation,
                       float contrast) {
    m_constants.colorTint = {tint.x, tint.y, tint.z, brightness};
    m_constants.colorParams = {saturation, contrast, 0, 0};
  }

  /**
   * @brief ビネット設定
   */
  void SetVignette(float intensity, float radius = 0.7f,
                   float softness = 0.5f) {
    m_constants.vignetteParams = {intensity, radius, softness, 0};
  }

  /**
   * @brief 定数バッファを更新してバインド
   */
  void BindConstants(ID3D11DeviceContext *context) {
    if (!m_initialized)
      return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      memcpy(mapped.pData, &m_constants, sizeof(m_constants));
      context->Unmap(m_constantBuffer.Get(), 0);
    }

    context->PSSetConstantBuffers(1, 1, m_constantBuffer.GetAddressOf());
    context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
  }

  /**
   * @brief 現在の定数を取得
   */
  const PostProcessConstants &GetConstants() const { return m_constants; }

  /**
   * @brief フルスクリーン描画用の準備
   */
  void PrepareFullscreenPass(ID3D11DeviceContext *context) {
    if (!m_initialized)
      return;

    UINT stride = sizeof(XMFLOAT3) + sizeof(XMFLOAT2);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride,
                                &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
  }

  /**
   * @brief フルスクリーン三角形を描画
   */
  void DrawFullscreenTriangle(ID3D11DeviceContext *context) {
    if (!m_initialized)
      return;
    context->Draw(3, 0);
  }

private:
  ID3D11Device *m_device = nullptr;
  ComPtr<ID3D11Buffer> m_constantBuffer;
  ComPtr<ID3D11Buffer> m_vertexBuffer;
  ComPtr<ID3D11SamplerState> m_samplerState;
  ComPtr<ID3D11BlendState> m_blendState;
  PostProcessConstants m_constants = {};
  bool m_initialized = false;
};

} // namespace game::systems
