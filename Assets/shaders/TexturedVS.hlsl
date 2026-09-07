/**
 * @file TexturedVS.hlsl
 * @brief 看板および画像テクスチャ用頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;          /**< ワールド行列 */
    matrix View;           /**< ビュー行列 */
    matrix Projection;     /**< 射影行列 */
    float4 Color;          /**< マテリアルカラー */
    float4 MaterialFlags;  /**< x: hasTex, y: normalMap(未使用), z: fadeFactor, w: effectIntensity */
    float4 LightDir;       /**< xyz: 光源方向, w: 経過秒数 */
};

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 Pos : POSITION;        /**< ローカル頂点座標 */
    float3 Normal : NORMAL;       /**< 法線ベクトル */
    float2 TexCoord : TEXCOORD0;  /**< UV座標 */
    float3 Tangent : TANGENT;     /**< 接線ベクトル */
    float3 Bitangent : BINORMAL;  /**< 従法線ベクトル */
};

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 Pos : SV_POSITION;           /**< 射影座標 */
    float3 Normal : NORMAL;             /**< ワールド法線 */
    float2 TexCoord : TEXCOORD0;        /**< UV座標 */
    float4 Color : COLOR;               /**< マテリアルカラー */
    float FadeFactor : TEXCOORD1;       /**< フェード係数 (1:通常, 0:消去) */
    float Time : TEXCOORD2;             /**< 演出用経過時間[秒] */
    float EffectIntensity : TEXCOORD3;  /**< 枠演出強度 (0～1) */
};

/**
 * @brief 看板頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 変換済み頂点出力
 */
PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;

    float4 worldPos = mul(float4(input.Pos, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Pos = mul(viewPos, Projection);

    output.Normal = mul(input.Normal, (float3x3)World);
    output.TexCoord = input.TexCoord;
    output.Color = Color;
    output.FadeFactor = MaterialFlags.z;
    output.Time = LightDir.w;
    output.EffectIntensity = MaterialFlags.w;

    return output;
}
