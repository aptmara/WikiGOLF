/**
 * @file WallVS.hlsl
 * @brief ステージ外周バリア用頂点シェーダー（Wikiコードストリーム演出の下準備）
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;          /**< ワールド行列 */
    matrix View;           /**< ビュー行列 */
    matrix Projection;     /**< 射影行列 */
    float4 Color;          /**< マテリアルカラー（壁の基本色） */
    float4 MaterialFlags;  /**< 未使用 */
    float4 LightDir;       /**< xyz: 光源方向（未使用）, w: 経過秒数 */
};

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 Pos : POSITION;        /**< ローカル頂点座標 */
    float3 Normal : NORMAL;       /**< 法線ベクトル */
    float2 TexCoord : TEXCOORD0;  /**< UV座標 */
    float3 Tangent : TANGENT;     /**< 接線ベクトル（未使用） */
    float3 Bitangent : BINORMAL;  /**< 従法線ベクトル（未使用） */
};

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 Pos : SV_POSITION;     /**< 射影座標 */
    float2 TexCoord : TEXCOORD0;  /**< UV座標（縦方向をストリーム位置に使う） */
    float4 Color : COLOR;         /**< マテリアルカラー */
    float3 WorldPos : TEXCOORD1;  /**< ワールド座標（レーンのばらつき用） */
    float Time : TEXCOORD2;       /**< 演出用経過時間[秒] */
    float3 WorldNormal : NORMAL;  /**< ワールド法線（コース外側の面を判定する） */
};

/**
 * @brief 外周バリア頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 変換済み頂点出力
 */
PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;

    float4 worldPos = mul(float4(input.Pos, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Pos = mul(viewPos, Projection);

    output.TexCoord = input.TexCoord;
    output.Color = Color;
    output.WorldPos = worldPos.xyz;
    output.Time = LightDir.w;
    output.WorldNormal = mul(input.Normal, (float3x3)World);

    return output;
}
