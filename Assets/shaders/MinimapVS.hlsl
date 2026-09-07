/**
 * @file MinimapVS.hlsl
 * @brief ミニマップ描画専用のインスタンシング対応頂点シェーダー
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
 * @brief ミニマップインスタンスデータ
 */
struct InstanceData {
    matrix World; /**< ワールド行列 */
    float4 Color; /**< インスタンスカラー */
    float4 Flags; /**< x: hasTexture, y: forceOpaque, z: uvScale, w: 未使用 */
};

/** @brief ミニマップインスタンスバッファ */
StructuredBuffer<InstanceData> g_instances : register(t15);

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 position : POSITION;      /**< 頂点ローカル座標 */
    float3 normal : NORMAL;          /**< 法線ベクトル */
    float2 texCoord : TEXCOORD;      /**< UV座標 */
    float4 color : COLOR;            /**< 頂点カラー */
    float3 tangent : TANGENT;        /**< 接線ベクトル */
    float3 bitangent : BINORMAL;     /**< 従法線ベクトル */
    uint instanceID : SV_InstanceID; /**< インスタンスID */
};

/**
 * @struct VS_OUTPUT
 * @brief 頂点シェーダー出力
 */
struct VS_OUTPUT {
    float4 position : SV_POSITION; /**< 射影座標 */
    float2 texCoord : TEXCOORD0;   /**< UV座標 */
    float4 color : COLOR;          /**< 頂点カラー */
    float4 flags : TEXCOORD1;      /**< フラグ情報 */
};

/**
 * @brief ミニマップ頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 正射影変換後の頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    InstanceData inst = g_instances[input.instanceID];

    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);

    output.texCoord = input.texCoord;

    float4 vertexColor = input.color;
    if (vertexColor.a <= 0.0001f && all(vertexColor.rgb == 0.0f)) {
        vertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    output.color = vertexColor * inst.Color;
    output.flags = inst.Flags;

    return output;
}
