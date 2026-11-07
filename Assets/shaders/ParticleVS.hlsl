/**
 * @file ParticleVS.hlsl
 * @brief パーティクル専用頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
    float4 LightDir;      // w: globalTime
};

/**
 * @struct InstanceData
 * @brief パーティクルインスタンスデータ
 */
struct InstanceData {
    matrix World; /**< ワールド行列 */
    float4 Color; /**< パーティクルカラー */
    float4 Flags; /**< x,y: texture flags, z: shape mode, w: variant */
    matrix PrevWorld; /**< 前フレームのワールド行列（BasicVSと共通のレイアウト。未使用） */
};

/** @brief パーティクルインスタンスバッファ */
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
    uint instanceID : SV_InstanceID; /**< インスタンスID */
};

/**
 * @struct VS_OUTPUT
 * @brief 頂点シェーダー出力
 */
struct VS_OUTPUT {
    float4 position : SV_POSITION;   /**< 射影座標 */
    float3 normal : NORMAL;          /**< 法線ベクトル */
    float2 texCoord : TEXCOORD;      /**< UV座標 */
    float4 color : COLOR;            /**< 頂点カラー */
    float4 materialFlags : TEXCOORD4;/**< マテリアルフラグ */
};

/**
 * @brief パーティクル頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return CPU側で設定した変換と透明度を保持した頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    InstanceData inst = g_instances[input.instanceID];
    
    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);
    
    float4 viewPos = mul(worldPos, View);
    if (inst.Flags.x > 0.5) {
        float3 axisX = inst.World[0].xyz;
        float3 axisY = inst.World[1].xyz;
        float3 axisZ = inst.World[2].xyz;
        float2 size = float2(length(axisX), max(length(axisY), length(axisZ)));
        if (inst.Flags.z > 5.5 && inst.Flags.z < 6.5) {
            size.y = length(axisZ);
        }
        float roll = atan2(axisX.y, axisX.x);
        float sn, cs;
        sincos(roll, sn, cs);
        float2 offset = input.position.xy * size;
        offset = float2(offset.x * cs - offset.y * sn,
                        offset.x * sn + offset.y * cs);
        viewPos = mul(float4(inst.World[3].xyz, 1.0), View);
        viewPos.xy += offset;
    }
    output.position = mul(viewPos, Projection);
    
    output.normal = mul(input.normal, (float3x3)inst.World);
    output.texCoord = input.texCoord;
    
    output.color = input.color * inst.Color;
    if (inst.Flags.z > 4.5 && inst.Flags.z < 5.5) {
        output.color.a *= saturate(input.normal.y);
    }
    output.materialFlags = inst.Flags;
    
    return output;
}
