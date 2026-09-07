/**
 * @file SkyboxVS.hlsl
 * @brief スカイボックス用頂点シェーダー
 */

cbuffer SkyboxConstants : register(b0) {
    matrix View;
    matrix Projection;
    float4 TintColor;
    float Brightness;
    float Saturation;
    float Time;
    float Padding;
    float3 SunDirection;
    float Padding2;
};

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
    float4 position : SV_POSITION; /**< 射影座標 (z=w) */
    float3 texCoord : TEXCOORD;    /**< キューブマップ方向ベクトル */
};

/**
 * @brief スカイボックス頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 平行移動除去・無限遠深度設定後の頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    // カメラの回転のみを適用（移動は無視）
    // Viewマトリックスから移動成分を除去
    matrix viewNoTranslation = View;
    viewNoTranslation._41 = 0.0f;
    viewNoTranslation._42 = 0.0f;
    viewNoTranslation._43 = 0.0f;
    
    // 頂点を変換
    float4 pos = mul(float4(input.position, 1.0f), viewNoTranslation);
    output.position = mul(pos, Projection);
    
    // 深度を最大にして常に背景として描画
    output.position.z = output.position.w;
    
    // テクスチャ座標として元の頂点位置を使用
    output.texCoord = input.position;
    
    return output;
}
