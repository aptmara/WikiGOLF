/**
 * @file SlopeOverlayVS.hlsl
 * @brief 傾斜可視化オーバーレイ用頂点シェーダー（非インスタンス描画）
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
};

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 position : POSITION;   /**< ローカル座標（構築時にワールド座標として書き込み済み） */
    float3 normal : NORMAL;       /**< 未使用（常に真上を向く前提） */
    float2 texCoord : TEXCOORD;   /**< 未使用 */
    float4 color : COLOR;         /**< rgb=傾斜カラー(青→赤), a=傾斜強度[0,1] */
    float3 tangent : TANGENT;     /**< 傾斜が下る方向（ワールドXZ、正規化、Y=0） */
    float3 bitangent : BINORMAL;  /**< 未使用 */
};

/**
 * @struct VS_OUTPUT
 * @brief 頂点シェーダー出力 / ピクセルシェーダー入力
 */
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 flowDir : TEXCOORD0;   /**< ワールド空間の傾斜下り方向 */
    float2 worldXZ : TEXCOORD1;   /**< ワールドXZ座標（流れる縞の位相計算用） */
};

/**
 * @brief 傾斜オーバーレイ頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 変換済み頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    float4 worldPos = mul(float4(input.position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);

    float3x3 world3x3 = (float3x3)World;
    output.flowDir = mul(input.tangent, world3x3);
    output.worldXZ = worldPos.xz;
    output.color = input.color;

    return output;
}
