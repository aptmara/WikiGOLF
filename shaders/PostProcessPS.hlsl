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

// 定数バッファ
cbuffer PostProcessConstants : register(b1) {
  float4 fogColor;      // RGB + density
  float4 fogParams;     // start, end, 0, 0
  float4 colorTint;     // RGB + brightness
  float4 colorParams;   // saturation, contrast, 0, 0
  float4 vignetteParams;// intensity, radius, softness, 0
  float4 timeParams;    // time, 0, 0, 0
};

// 彩度調整
float3 AdjustSaturation(float3 color, float saturation) {
  float luminance = dot(color, float3(0.299, 0.587, 0.114));
  return lerp(float3(luminance, luminance, luminance), color, saturation);
}

// コントラスト調整
float3 AdjustContrast(float3 color, float contrast) {
  return (color - 0.5) * contrast + 0.5;
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
  
  // === 霧効果（深度バッファがある場合のみ） ===
  // 注: 深度バッファがない場合は霧はスキップ
  // float depth = depthTexture.Sample(samplerState, uv).r;
  // float fogFactor = saturate((depth - fogParams.x) / (fogParams.y - fogParams.x));
  // color = lerp(color, fogColor.rgb, fogFactor * fogColor.w);
  
  // === 色調補正 ===
  // ティント
  color *= colorTint.rgb;
  
  // 明度
  color *= colorTint.w;
  
  // 彩度
  color = AdjustSaturation(color, colorParams.x);
  
  // コントラスト
  color = AdjustContrast(color, colorParams.y);
  
  // === ビネット ===
  float vignette = ComputeVignette(uv, vignetteParams.x, vignetteParams.y, vignetteParams.z);
  color *= vignette;
  
  // クランプ
  color = saturate(color);
  
  return float4(color, sceneColor.a);
}
