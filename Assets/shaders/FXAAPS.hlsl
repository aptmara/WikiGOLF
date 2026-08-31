/**
 * @file FXAAPS.hlsl
 * @brief 簡易FXAA（輝度ベースのエッジ検出＋方向性ブラー）。
 * @details フルスクリーンのポストエフェクトとして、内部描画解像度のシーンテクスチャに
 *          適用する。NVIDIA FXAA 3.11の考え方を単純化した軽量版。
 */

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FXAAConstants : register(b0) {
  float4 texelSize; // x = 1/renderWidth, y = 1/renderHeight
};

struct PSInput {
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
};

float Luma(float3 c) {
  return dot(c, float3(0.299, 0.587, 0.114));
}

float4 main(PSInput input) : SV_TARGET {
  float2 uv = input.texCoord;
  float2 texel = texelSize.xy;

  float3 colorCenter = sceneTexture.Sample(linearSampler, uv).rgb;

  float lumaTL = Luma(sceneTexture.Sample(linearSampler, uv + texel * float2(-1.0, -1.0)).rgb);
  float lumaTR = Luma(sceneTexture.Sample(linearSampler, uv + texel * float2( 1.0, -1.0)).rgb);
  float lumaBL = Luma(sceneTexture.Sample(linearSampler, uv + texel * float2(-1.0,  1.0)).rgb);
  float lumaBR = Luma(sceneTexture.Sample(linearSampler, uv + texel * float2( 1.0,  1.0)).rgb);
  float lumaM  = Luma(colorCenter);

  float lumaMin = min(lumaM, min(min(lumaTL, lumaTR), min(lumaBL, lumaBR)));
  float lumaMax = max(lumaM, max(max(lumaTL, lumaTR), max(lumaBL, lumaBR)));
  float range = lumaMax - lumaMin;

  static const float kEdgeThresholdMin = 0.0312;
  static const float kEdgeThreshold = 0.125;
  float threshold = max(kEdgeThresholdMin, lumaMax * kEdgeThreshold);

  if (range < threshold) {
    return float4(colorCenter, 1.0);
  }

  // 輝度勾配からエッジの向きを推定する
  float2 dir;
  dir.x = -((lumaTL + lumaTR) - (lumaBL + lumaBR));
  dir.y =  ((lumaTL + lumaBL) - (lumaTR + lumaBR));

  float dirReduce = max((lumaTL + lumaTR + lumaBL + lumaBR) * 0.25 * 0.25, 1.0 / 128.0);
  float dirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * dirMin, -8.0, 8.0) * texel;

  float3 blurNear = 0.5 * (
      sceneTexture.Sample(linearSampler, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
      sceneTexture.Sample(linearSampler, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
  float3 blurFar = blurNear * 0.5 + 0.25 * (
      sceneTexture.Sample(linearSampler, uv + dir * (0.0 / 3.0 - 0.5)).rgb +
      sceneTexture.Sample(linearSampler, uv + dir * (3.0 / 3.0 - 0.5)).rgb);

  float lumaBlurFar = Luma(blurFar);
  float3 result = (lumaBlurFar < lumaMin || lumaBlurFar > lumaMax) ? blurNear : blurFar;

  return float4(result, 1.0);
}
