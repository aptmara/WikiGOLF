/**
 * @file BasicVS.hlsl
 * @brief 基本頂点シェーダー（インスタンシング対応）
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
};

/**
 * @struct InstanceData
 * @brief インスタンシング描画用データ
 */
struct InstanceData {
    matrix World;      /**< ワールド変換行列 */
    float4 Color;      /**< 乗算カラー */
    float4 Flags;      /**< マテリアルフラグ群 */
};

/** @brief インスタンスデータバッファ */
StructuredBuffer<InstanceData> g_instances : register(t15);

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 position : POSITION;      /**< ローカル座標 */
    float3 normal : NORMAL;          /**< 法線ベクトル */
    float2 texCoord : TEXCOORD;      /**< UV座標 */
    float4 color : COLOR;            /**< 頂点カラー */
    float3 tangent : TANGENT;        /**< 接線ベクトル */
    float3 bitangent : BINORMAL;     /**< 従法線ベクトル */
    uint instanceID : SV_InstanceID; /**< インスタンスID */
};

/**
 * @struct VS_OUTPUT
 * @brief 頂点シェーダー出力 / ピクセルシェーダー入力
 */
struct VS_OUTPUT {
    float4 position : SV_POSITION;   /**< 射影座標 */
    float3 normal : NORMAL;          /**< ワールド法線 */
    float2 texCoord : TEXCOORD;      /**< UV座標 */
    float4 color : COLOR;            /**< 頂点カラー */
    float3 tangent : TANGENT;        /**< ワールド接線 */
    float3 bitangent : BINORMAL;     /**< ワールド従法線 */
    float2 worldXZ : TEXCOORD1;      /**< ワールドXZ座標 */
    float4 materialFlags : TEXCOORD4;/**< マテリアルフラグ */
};

/**
 * @brief 基本頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 変換済み頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    InstanceData inst = g_instances[input.instanceID];
    
    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);
    
    float3x3 world3x3 = (float3x3)inst.World;
    output.normal = mul(input.normal, world3x3);
    output.tangent = mul(input.tangent, world3x3);
    output.bitangent = mul(input.bitangent, world3x3);
    output.texCoord = input.texCoord;
    
    float4 vcolor = (input.color.a <= 0.0001f && all(input.color.rgb == 0)) ?
                    float4(1,1,1,1) : input.color;
    output.color = vcolor * inst.Color;

    output.worldXZ = worldPos.xz;
    output.materialFlags = inst.Flags;
    output.materialFlags.w = inst.Color.a;
    
    return output;
}
