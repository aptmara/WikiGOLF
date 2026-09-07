/**
 * @file TerrainVS.hlsl
 * @brief 地形メッシュ用頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;          /**< ワールド行列 */
    matrix View;           /**< ビュー行列 */
    matrix Projection;     /**< 射影行列 */
    float4 Color;          /**< マテリアルカラー */
    float4 MaterialFlags;  /**< x: hasTexture, y: hasNormalMap, z: uvScale */
    float4 LightDir;       /**< 光源方向 */
    float4 CameraPos;      /**< カメラワールド座標 */
};

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 Pos : POSITION;       /**< ローカル頂点座標 */
    float3 Normal : NORMAL;      /**< 頂点法線 */
    float2 Tex : TEXCOORD0;      /**< UV座標 */
    float4 Color : COLOR0;       /**< 頂点カラー（マテリアルID格納） */
    float3 Tangent : TANGENT;    /**< 接線ベクトル */
    float3 Bitangent : BINORMAL; /**< 従法線ベクトル */
};

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 Pos : SV_POSITION;    /**< 射影座標 */
    float3 WorldPos : POSITION;  /**< ワールド座標 */
    float3 Normal : NORMAL;      /**< ワールド法線 */
    float2 Tex : TEXCOORD0;      /**< UV座標 */
    float4 Color : COLOR0;       /**< 頂点カラー（マテリアルブレンド用） */
    float3 Tangent : TANGENT;    /**< ワールド接線 */
    float3 Bitangent : BINORMAL; /**< ワールド従法線 */
};

/**
 * @brief 地形頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 変換済み頂点出力
 */
PS_INPUT main(VS_INPUT input) {
    PS_INPUT output = (PS_INPUT)0;
    
    float4 pos = float4(input.Pos, 1.0f);
    float4 worldPos = mul(pos, World);
    output.WorldPos = worldPos.xyz;
    
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);
    
    float3x3 world3x3 = (float3x3)World;
    output.Normal = normalize(mul(input.Normal, world3x3));
    output.Tangent = normalize(mul(input.Tangent, world3x3));
    output.Bitangent = normalize(mul(input.Bitangent, world3x3));
    
    output.Tex = input.Tex;
    output.Color = input.Color * Color;
    
    return output;
}
