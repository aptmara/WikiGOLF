/**
 * @file TAAPS.hlsl
 * @brief テンポラルアンチエイリアス／アップスケール（TAA・TAAU）解決パス
 * @details 投影行列へサブピクセルジッターを加えて描画解像度で描いた現フレームを、
 *          出力解像度の履歴へ蓄積する。描画解像度が出力より低い場合は、各出力画素に
 *          最も近いジッター位置のサンプルほど重く混ぜることで、数フレームかけて
 *          出力解像度の情報を復元する（TAAU）。
 *          履歴は解決済み速度（物体の動き＋カメラの動き）で追従し、近傍色の
 *          分布でクリップしてゴースト（残像）を抑える。
 */

struct PSInput {
  float4 position : SV_POSITION; /**< 射影座標（出力解像度） */
  float2 texCoord : TEXCOORD0;   /**< UV座標 */
};

Texture2D currentTexture : register(t0);      /**< 現フレーム（描画解像度・ジッター付き） */
Texture2D velocityTexture : register(t1);     /**< 解決済み速度（描画解像度, UV単位） */
Texture2D<float> depthTexture : register(t2); /**< 深度（描画解像度） */
Texture2D historyTexture : register(t3);      /**< 前フレームまでの蓄積結果（出力解像度） */
SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);

cbuffer TaaConstants : register(b0) {
  float4 renderSize; /**< 描画解像度 w, h, 1/w, 1/h */
  float4 outputSize; /**< 出力解像度 w, h, 1/w, 1/h */
  float4 jitterUv;   /**< xy: ジッター(UV)、z: 履歴を使うか(1/0) */
  float4 params;     /**< 未使用 */
};

static const float kBaseBlend = 0.1;         /**< 静止時に現フレームを混ぜる最大割合 */
static const float kMotionBlend = 0.25;      /**< 大きく動いたときの割合（ブレ・残像抑制） */
static const float kMotionBlendPixels = 6.0; /**< kMotionBlendに達する移動量[出力px] */
static const float kMinBlend = 0.02;         /**< 履歴が張り付かないための下限 */
static const float kClipGamma = 1.25;        /**< 分散クリップの幅（標準偏差の倍率） */

float3 RgbToYCoCg(float3 c) {
  return float3(dot(c, float3(0.25, 0.5, 0.25)),
                dot(c, float3(0.5, 0.0, -0.5)),
                dot(c, float3(-0.25, 0.5, -0.25)));
}

float3 YCoCgToRgb(float3 c) {
  return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

/** @brief 5回のバイリニア取得で近似したCatmull-Rom補間（履歴の再サンプリングによるぼけを抑える） */
float3 SampleHistoryCatmullRom(float2 uv) {
  float2 textureSize = outputSize.xy;
  float2 samplePos = uv * textureSize;
  float2 texPos1 = floor(samplePos - 0.5) + 0.5;
  float2 f = samplePos - texPos1;

  float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  float2 w3 = f * f * (-0.5 + 0.5 * f);

  float2 w12 = w1 + w2;
  float2 offset12 = w2 / w12;

  float2 texPos0 = (texPos1 - 1.0) * outputSize.zw;
  float2 texPos3 = (texPos1 + 2.0) * outputSize.zw;
  float2 texPos12 = (texPos1 + offset12) * outputSize.zw;

  float3 result = 0.0;
  float weightSum = 0.0;
  float w;

  w = w12.x * w0.y;
  result += historyTexture.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0).rgb * w;
  weightSum += w;
  w = w0.x * w12.y;
  result += historyTexture.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0).rgb * w;
  weightSum += w;
  w = w12.x * w12.y;
  result += historyTexture.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0).rgb * w;
  weightSum += w;
  w = w3.x * w12.y;
  result += historyTexture.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0).rgb * w;
  weightSum += w;
  w = w12.x * w3.y;
  result += historyTexture.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0).rgb * w;
  weightSum += w;

  return max(result / max(weightSum, 0.0001), 0.0);
}

/** @brief 履歴色を近傍色のAABBへ向けてクリップする（中心方向の線分と箱の交点） */
float3 ClipToAabb(float3 history, float3 center, float3 boxMin, float3 boxMax) {
  float3 extent = max((boxMax - boxMin) * 0.5, 0.0001);
  float3 offset = history - center;
  float3 ratio = abs(offset / extent);
  float maxRatio = max(ratio.x, max(ratio.y, ratio.z));
  if (maxRatio > 1.0) {
    return center + offset / maxRatio;
  }
  return history;
}

float4 main(PSInput input) : SV_TARGET {
  // 出力画素中心のジッター無しUV
  float2 uv = input.position.xy * outputSize.zw;
  // 同じ表面が今フレームの描画結果のどこに写っているか（描画解像度のピクセル座標）
  float2 renderPos = (uv + jitterUv.xy) * renderSize.xy;
  int2 maxCoord = int2(renderSize.xy) - 1;
  int2 centerPixel = clamp(int2(floor(renderPos)), int2(0, 0), maxCoord);

  // 3x3近傍: 再構成フィルタ、色の統計（YCoCg）、最も手前の深度の画素を集める
  float3 filtered = 0.0;
  float filterWeightSum = 0.0;
  float confidence = 0.0;
  float3 moment1 = 0.0;
  float3 moment2 = 0.0;
  float3 boxMin = 1e5;
  float3 boxMax = -1e5;
  float closestDepth = 1.0;
  int2 closestPixel = centerPixel;
  float currentAlpha = 1.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      int2 coord = clamp(centerPixel + int2(x, y), int2(0, 0), maxCoord);
      float4 sampleColor = currentTexture.Load(int3(coord, 0));
      float3 c = RgbToYCoCg(sampleColor.rgb);

      // 描画サンプルの位置と、この出力画素の位置との距離でガウス重み付けする
      float2 delta = (float2(coord) + 0.5) - renderPos;
      float weight = exp(-2.29 * dot(delta, delta));
      filtered += c * weight;
      filterWeightSum += weight;
      if (weight > confidence) {
        confidence = weight;
        currentAlpha = sampleColor.a;
      }

      moment1 += c;
      moment2 += c * c;
      boxMin = min(boxMin, c);
      boxMax = max(boxMax, c);

      float depth = depthTexture.Load(int3(coord, 0));
      if (depth < closestDepth) {
        closestDepth = depth;
        closestPixel = coord;
      }
    }
  }
  float3 current = filtered / max(filterWeightSum, 0.0001);

  if (jitterUv.z < 0.5) {
    return float4(saturate(YCoCgToRgb(current)), currentAlpha);
  }

  // 最も手前の画素の速度を使うと、物体の輪郭で背景側の動きを拾って縁が崩れるのを防げる
  float2 velocity = velocityTexture.Load(int3(closestPixel, 0)).xy;
  float2 prevUv = uv - velocity;
  if (any(prevUv < 0.0) || any(prevUv > 1.0)) {
    return float4(saturate(YCoCgToRgb(current)), currentAlpha); // 画面外から入ってきた領域
  }

  float3 history = RgbToYCoCg(SampleHistoryCatmullRom(prevUv));

  // 分散クリップ（min/maxの箱と平均±標準偏差の箱の共通部分）
  float3 mean = moment1 / 9.0;
  float3 sigma = sqrt(abs(moment2 / 9.0 - mean * mean));
  float3 clipMin = max(boxMin, mean - kClipGamma * sigma);
  float3 clipMax = min(boxMax, mean + kClipGamma * sigma);
  history = ClipToAabb(history, clamp(mean, clipMin, clipMax), clipMin, clipMax);

  // 移動量が大きいほど現フレームを重視して、ぼけと残像を減らす。
  // 描画サンプルがこの出力画素から離れているほど（アップスケール時）現フレームを弱める。
  float motionPixels = length(velocity * outputSize.xy);
  float blend = lerp(kBaseBlend, kMotionBlend, saturate(motionPixels / kMotionBlendPixels));
  blend = max(blend * confidence, kMinBlend);

  // 輝度重み付け: 明るい1ピクセルの点滅（ハイライトのちらつき）を抑える
  float currentWeight = blend / (1.0 + current.x);
  float historyWeight = (1.0 - blend) / (1.0 + history.x);
  float3 resolved = (current * currentWeight + history * historyWeight) /
                    max(currentWeight + historyWeight, 0.0001);

  return float4(saturate(YCoCgToRgb(resolved)), currentAlpha);
}
