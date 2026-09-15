/**
 * @file VelocityResolvePS.hlsl
 * @brief 速度バッファの解決パス（TAA/DLSS共通の入力を作る）
 * @details メッシュ描画で書かれた速度(a=1)はそのまま使い、書かれていない画素
 *          (a=0: スカイボックス・静止物)は深度からワールド座標を復元して
 *          前フレームのView*Projectionで再投影し、カメラ移動分の速度を求める。
 *          出力は「今フレームのUV − 前フレームのUV」（ジッター除去済み）。
 */

struct PSInput {
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
};

Texture2D velocityTexture : register(t0);      /**< メッシュ描画で書いた速度 */
Texture2D<float> depthTexture : register(t1);  /**< シーン深度 */

cbuffer VelocityResolveConstants : register(b0) {
  matrix invViewProjection;  /**< 今フレームView*Projection（ジッター無し）の逆行列 */
  matrix prevViewProjection; /**< 前フレームView*Projection（ジッター無し） */
  float4 jitterUv;           /**< xy: ジッター(UV)、zw: 1/描画解像度 */
  float4 params;             /**< x: 前フレームのカメラが有効(1/0) */
};

float4 main(PSInput input) : SV_TARGET {
  int2 pixel = int2(input.position.xy);
  float4 written = velocityTexture.Load(int3(pixel, 0));
  if (written.a > 0.5) {
    return float4(written.xy, 0.0, 0.0);
  }
  if (params.x < 0.5) {
    return float4(0.0, 0.0, 0.0, 0.0);
  }

  float depth = depthTexture.Load(int3(pixel, 0));
  float2 uv = (float2(pixel) + 0.5) * jitterUv.zw - jitterUv.xy;
  float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  float4 worldPos = mul(float4(ndc, depth, 1.0), invViewProjection);
  worldPos /= worldPos.w;
  float4 prevClip = mul(worldPos, prevViewProjection);
  if (prevClip.w <= 0.0001) {
    return float4(0.0, 0.0, 0.0, 0.0);
  }
  float2 prevNdc = prevClip.xy / prevClip.w;
  float2 prevUv = float2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);
  return float4(uv - prevUv, 0.0, 0.0);
}
