/**
 * @file FullscreenVS.hlsl
 * @brief 全画面クアッド用の頂点シェーダー（TransitionPSなどのフルスクリーン効果向け）
 */

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 position : POSITION;  /**< ローカル頂点座標 */
    float3 normal : NORMAL;      /**< 法線ベクトル */
    float2 texCoord : TEXCOORD;  /**< UV座標 */
    float4 color : COLOR;        /**< 頂点カラー */
    float3 tangent : TANGENT;    /**< 接線ベクトル */
    float3 bitangent : BINORMAL; /**< 従法線ベクトル */
};

/**
 * @struct VS_OUTPUT
 * @brief 頂点シェーダー出力
 */
struct VS_OUTPUT {
    float4 position : SV_POSITION; /**< 射影座標 */
    float2 texCoord : TEXCOORD0;   /**< UV座標 */
};

/**
 * @brief フルスクリーン頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 全画面クアッド頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    // builtin/quad は -0.5〜0.5 の範囲なので、2倍してクリップ空間全体を覆う
    output.position = float4(input.position.xy * 2.0f, 0.0f, 1.0f);
    output.texCoord = input.texCoord;

    return output;
}
