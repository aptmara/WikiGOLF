/**
 * @file PostProcessPS.hlsl
 * @brief ポストプロセス ピクセルシェーダー - 霧、色調補正、ビネット
 */

// 入力
struct PSInput {
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
};

// テクスチャ
Texture2D sceneTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState samplerState : register(s0);

// 定数バッファ（graphics::PostProcessParamsと1:1対応）
cbuffer PostProcessConstants : register(b0) {
  float4 fogColor;      // RGB + density
  float4 fogParams;     // start, end, 0, 0
  float4 colorTint;     // RGB + brightness
  float4 colorParams;   // saturation, contrast, 0, 0
  float4 vignetteParams;// intensity, radius, softness, 0
  float4 timeParams;    // time, bloomIntensity, bloomThreshold, bloomSpread
  float4 depthParams;   // nearZ, farZ, hasDepth(1/0), 0
};

// LH・zレンジ[0,1]の非線形深度をビュー空間の線形深度へ変換する
float LinearizeDepth(float depthNDC, float nearZ, float farZ) {
  return (nearZ * farZ) / (farZ - depthNDC * (farZ - nearZ));
}

// 彩度調整
float3 AdjustSaturation(float3 color, float saturation) {
  float luminance = dot(color, float3(0.299, 0.587, 0.114));
  return lerp(float3(luminance, luminance, luminance), color, saturation);
}

// コントラスト調整
float3 AdjustContrast(float3 color, float contrast) {
  return (color - 0.5) * contrast + 0.5;
}

// ブルーム用の明るい色抽出
float3 ExtractBloom(float3 color, float threshold) {
  float brightness = max(color.r, max(color.g, color.b));
  float amount = saturate((brightness - threshold) / max(1.0 - threshold, 0.001));
  return color * amount;
}

// ビネット効果
float ComputeVignette(float2 uv, float intensity, float radius, float softness) {
  float2 center = uv - 0.5;
  float dist = length(center);
  float vignette = 1.0 - smoothstep(radius - softness, radius + softness, dist);
  return lerp(1.0, vignette, intensity);
}

// メイン
float4 main(PSInput input) : SV_TARGET {
  float2 uv = input.texCoord;
  
  // シーンカラー取得
  float4 sceneColor = sceneTexture.Sample(samplerState, uv);
  float3 color = sceneColor.rgb;
  
  // === 霧効果（深度バッファが読める場合のみ。MSAA有効時は常に無効） ===
  if (depthParams.z > 0.5) {
    float rawDepth = depthTexture.Sample(samplerState, uv).r;
    float linearDepth = LinearizeDepth(rawDepth, depthParams.x, depthParams.y);
    float fogRange = max(fogParams.y - fogParams.x, 0.0001);
    float fogFactor = saturate((linearDepth - fogParams.x) / fogRange);
    color = lerp(color, fogColor.rgb, fogFactor * fogColor.w);
  }

  // === 色調補正 ===
  // ティント
  color *= colorTint.rgb;
  
  // 明度
  color *= colorTint.w;
  
  // 彩度
  color = AdjustSaturation(color, colorParams.x);
  
  // コントラスト
  color = AdjustContrast(color, colorParams.y);

  // === 簡易ブルーム ===
  float bloomIntensity = timeParams.y;
  if (bloomIntensity > 0.001) {
    float threshold = timeParams.z;
    float spread = max(timeParams.w, 0.1);
    float2 texel = max(abs(ddx(uv)) + abs(ddy(uv)),
                       float2(1.0 / 4096.0, 1.0 / 4096.0)) * spread;

    float3 bloom = ExtractBloom(sceneColor.rgb, threshold) * 0.32;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2( 1,  0)).rgb, threshold) * 0.14;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2(-1,  0)).rgb, threshold) * 0.14;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2( 0,  1)).rgb, threshold) * 0.14;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2( 0, -1)).rgb, threshold) * 0.14;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2( 2,  2)).rgb, threshold) * 0.06;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2(-2,  2)).rgb, threshold) * 0.06;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2( 2, -2)).rgb, threshold) * 0.06;
    bloom += ExtractBloom(sceneTexture.Sample(samplerState, uv + texel * float2(-2, -2)).rgb, threshold) * 0.06;

    color += bloom * bloomIntensity;
  }
  
  // === ビネット ===
  float vignette = ComputeVignette(uv, vignetteParams.x, vignetteParams.y, vignetteParams.z);
  color *= vignette;
  
  // クランプ
  color = saturate(color);
  
  return float4(color, sceneColor.a);
}
