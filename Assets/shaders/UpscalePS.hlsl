/**
 * @file UpscalePS.hlsl
 * @brief 内部描画解像度(Render Scale適用後)のシーンテクスチャを出力解像度へ
 *        拡大/縮小して転送する。Render Scaleで縮小した分だけ軽量な
 *        アンシャープマスクで輪郭を強調し、アップスケールのぼやけを補う。
 */

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer UpscaleConstants : register(b0) {
  // x=1/renderWidth, y=1/renderHeight, z=シャープ強度(0で無効), w=未使用
  float4 texelSize;
};

struct PSInput {
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
  float2 uv = input.texCoord;
  float3 center = sceneTexture.Sample(linearSampler, uv).rgb;

  float sharpenAmount = texelSize.z;
  if (sharpenAmount <= 0.001) {
    return float4(center, 1.0);
  }

  float2 texel = texelSize.xy;
  float3 up = sceneTexture.Sample(linearSampler, uv + float2(0.0, -texel.y)).rgb;
  float3 down = sceneTexture.Sample(linearSampler, uv + float2(0.0, texel.y)).rgb;
  float3 left = sceneTexture.Sample(linearSampler, uv + float2(-texel.x, 0.0)).rgb;
  float3 right = sceneTexture.Sample(linearSampler, uv + float2(texel.x, 0.0)).rgb;

  float3 blur = (up + down + left + right) * 0.25;
  float3 sharpened = center + (center - blur) * sharpenAmount;

  return float4(saturate(sharpened), 1.0);
}
