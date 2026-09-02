/**
 * @file FXAAPS.hlsl
 * @brief 簡易FXAA（輝度ベースのエッジ検出＋方向性ブラー＋サブピクセルAA）。
 * @details フルスクリーンのポストエフェクトとして、内部描画解像度のシーンテクスチャに
 *          適用する。NVIDIA FXAA 3.11の考え方を単純化した軽量版。
 *          エッジ方向のブラーに加え、周囲8サンプルの平均輝度とのコントラストから
 *          サブピクセルアンチエイリアシング量を求めてブレンドすることで、
 *          細い線や遠景の高周波ディテールのちらつきも軽減する。
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

  float3 colorN = sceneTexture.Sample(linearSampler, uv + texel * float2( 0.0, -1.0)).rgb;
  float3 colorS = sceneTexture.Sample(linearSampler, uv + texel * float2( 0.0,  1.0)).rgb;
  float3 colorE = sceneTexture.Sample(linearSampler, uv + texel * float2( 1.0,  0.0)).rgb;
  float3 colorW = sceneTexture.Sample(linearSampler, uv + texel * float2(-1.0,  0.0)).rgb;
  float3 colorTL = sceneTexture.Sample(linearSampler, uv + texel * float2(-1.0, -1.0)).rgb;
  float3 colorTR = sceneTexture.Sample(linearSampler, uv + texel * float2( 1.0, -1.0)).rgb;
  float3 colorBL = sceneTexture.Sample(linearSampler, uv + texel * float2(-1.0,  1.0)).rgb;
  float3 colorBR = sceneTexture.Sample(linearSampler, uv + texel * float2( 1.0,  1.0)).rgb;

  float lumaM  = Luma(colorCenter);
  float lumaN  = Luma(colorN);
  float lumaS  = Luma(colorS);
  float lumaE  = Luma(colorE);
  float lumaW  = Luma(colorW);
  float lumaTL = Luma(colorTL);
  float lumaTR = Luma(colorTR);
  float lumaBL = Luma(colorBL);
  float lumaBR = Luma(colorBR);

  float lumaMin = min(lumaM, min(min(lumaTL, lumaTR), min(lumaBL, lumaBR)));
  float lumaMax = max(lumaM, max(max(lumaTL, lumaTR), max(lumaBL, lumaBR)));
  float range = lumaMax - lumaMin;

  static const float kEdgeThresholdMin = 0.0312;
  static const float kEdgeThreshold = 0.125;
  float threshold = max(kEdgeThresholdMin, lumaMax * kEdgeThreshold);

  if (range < threshold) {
    return float4(colorCenter, 1.0);
  }

  // --- サブピクセルAA量: 周囲8サンプルの平均輝度と中心との差が小さいほど、
  //     細い線・遠景ディテール由来のノイズとみなしてブレンド量を増やす。
  float lumaAvg8 = (lumaN + lumaS + lumaE + lumaW +
                    lumaTL + lumaTR + lumaBL + lumaBR) * 0.125;
  float subpixelContrast = saturate(abs(lumaAvg8 - lumaM) / max(range, 1e-4));
  float subpixelBlend = subpixelContrast * subpixelContrast * 0.75;
  float3 colorAvg8 = (colorN + colorS + colorE + colorW +
                      colorTL + colorTR + colorBL + colorBR) * 0.125;

  // --- エッジ方向を推定して軽くブラー（方向性AA） ---
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
  float3 directionalResult = (lumaBlurFar < lumaMin || lumaBlurFar > lumaMax) ? blurNear : blurFar;

  // 方向性AAの結果とサブピクセルAA（周囲8サンプル平均）をブレンドして最終結果とする
  float3 result = lerp(directionalResult, colorAvg8, subpixelBlend);

  return float4(result, 1.0);
}
