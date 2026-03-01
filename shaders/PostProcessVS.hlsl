/**
 * @file PostProcessVS.hlsl
 * @brief ポストプロセス 頂点シェーダー - フルスクリーン三角形
 */

// 入力
struct VSInput {
  float3 position : POSITION;
  float2 texCoord : TEXCOORD0;
};

// 出力
struct VSOutput {
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
};

// メイン
VSOutput main(VSInput input) {
  VSOutput output;
  output.position = float4(input.position, 1.0);
  output.texCoord = input.texCoord;
  return output;
}
