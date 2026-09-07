/**
 * @file PostProcessVS.hlsl
 * @brief ポストプロセス 頂点シェーダー - フルスクリーン三角形
 */

/**
 * @struct VSInput
 * @brief 頂点シェーダー入力
 */
struct VSInput {
  float3 position : POSITION;  /**< ローカル頂点座標 */
  float2 texCoord : TEXCOORD0; /**< UV座標 */
};

/**
 * @struct VSOutput
 * @brief 頂点シェーダー出力
 */
struct VSOutput {
  float4 position : SV_POSITION; /**< 射影座標 */
  float2 texCoord : TEXCOORD0;   /**< UV座標 */
};

/**
 * @brief ポストプロセス頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 全画面パス頂点出力
 */
VSOutput main(VSInput input) {
  VSOutput output;
  output.position = float4(input.position, 1.0);
  output.texCoord = input.texCoord;
  return output;
}
