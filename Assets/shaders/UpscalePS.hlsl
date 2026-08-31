/**
 * @file UpscalePS.hlsl
 * @brief 内部描画解像度(Render Scale適用後)のシーンテクスチャを
 *        出力解像度へ拡大/縮小して転送するだけのシンプルなピクセルシェーダー。
 */

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

struct PSInput {
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
  return sceneTexture.Sample(linearSampler, input.texCoord);
}
