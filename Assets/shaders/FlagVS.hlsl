/**
 * @file FlagVS.hlsl
 * @brief プロシージャル旗布用頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
    float4 LightDir;
    float4 CameraPos;
};

/**
 * @struct InstanceData
 * @brief インスタンシング描画用データ
 */
struct InstanceData {
    matrix World;      /**< ワールド行列 */
    float4 Color;      /**< 乗算カラー */
    float4 Flags;      /**< x: phase, y: amplitude, z: speedFactor, w: yaw */
    matrix PrevWorld;  /**< 前フレームのワールド行列（速度バッファ用） */
};

/** @brief インスタンスデータバッファ */
StructuredBuffer<InstanceData> g_instances : register(t15);

#include "TemporalVelocity.hlsli"

/**
 * @struct VS_INPUT
 * @brief 頂点シェーダー入力
 */
struct VS_INPUT {
    float3 position : POSITION;      /**< 頂点座標 */
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
    float4 position : SV_POSITION;   /**< 射影座標 */
    float3 normal : NORMAL;          /**< ワールド法線 */
    float2 texCoord : TEXCOORD;      /**< UV座標 */
    float4 color : COLOR;            /**< 頂点カラー */
    float4 prevClip : TEXCOORD1;     /**< 前フレームのクリップ座標（速度バッファ用） */
};

/**
 * @brief 指定時刻での、はためき・ヨー回転適用後のローカル座標を求めます。
 * @details 速度バッファ用に前フレームの時刻でも同じ式を評価するため関数化している。
 */
float3 FlagLocalPosition(float3 position, float2 texCoord, float4 flags,
                         float globalTime) {
    float u = saturate(texCoord.x);
    float v = texCoord.y;

    float phase = flags.x;
    float amplitude = max(flags.y, 0.05f);
    float speedFactor = flags.z;
    float yaw = flags.w;

    float timePhase = globalTime * (1.9f + speedFactor * 4.6f) + phase;
    // 無風時はほぼ静止し、風速が上がるほどはためきが強くなるよう
    // 芝(GrassVS.hlsl)と同じ考え方でamplitudeを風速に連動させる。
    float windAmplitude = amplitude * (0.18f + speedFactor * 1.32f);

    float tipWeight = u * u * (3.0f - 2.0f * u);
    float freeEdge = smoothstep(0.62f, 1.0f, u);
    float verticalBend = 1.0f - abs(v - 0.5f) * 2.0f;

    float primary = sin(timePhase * 3.35f - u * 7.6f);
    float secondary = sin(timePhase * 6.20f - u * 13.5f + v * 2.4f);
    float edgeRipple = sin(timePhase * 9.8f - u * 22.0f) * freeEdge;

    float3 localPos = position;
    localPos.x += (0.055f * windAmplitude + primary * 0.014f) * tipWeight;
    localPos.z += (primary * 0.150f + secondary * 0.060f + edgeRipple * 0.044f) * windAmplitude * tipWeight;
    localPos.y += (secondary * 0.030f + edgeRipple * 0.036f) * windAmplitude * tipWeight;
    localPos.y -= 0.018f * windAmplitude * tipWeight * verticalBend;

    // Rotate around Y by yaw
    float cosYaw = cos(yaw);
    float sinYaw = sin(yaw);
    float3 rotatedPos = localPos;
    rotatedPos.x = localPos.x * cosYaw - localPos.z * sinYaw;
    rotatedPos.z = localPos.x * sinYaw + localPos.z * cosYaw;
    return rotatedPos;
}

/**
 * @brief 旗布頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return はためき波形変形後の頂点出力
 */
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    InstanceData inst = g_instances[input.instanceID];

    float u = saturate(input.texCoord.x);
    float v = input.texCoord.y;

    float globalTime = LightDir.w;
    float phase = inst.Flags.x;
    float amplitude = max(inst.Flags.y, 0.05f);
    float speedFactor = inst.Flags.z;
    float yaw = inst.Flags.w;

    float timePhase = globalTime * (1.9f + speedFactor * 4.6f) + phase;
    float windAmplitude = amplitude * (0.18f + speedFactor * 1.32f);
    float tipWeight = u * u * (3.0f - 2.0f * u);

    float3 rotatedPos = FlagLocalPosition(input.position, input.texCoord,
                                          inst.Flags, globalTime);

    float cosYaw = cos(yaw);
    float sinYaw = sin(yaw);
    float dzdx = (cos(timePhase * 3.35f - u * 7.6f) * -7.6f * 0.150f +
                  cos(timePhase * 6.20f - u * 13.5f + v * 2.4f) * -13.5f * 0.060f) *
                 windAmplitude * max(tipWeight, 0.08f);
    float dzdy = cos(timePhase * 6.20f - u * 13.5f + v * 2.4f) * 2.4f * 0.060f * windAmplitude * tipWeight;
    float3 localNormal = normalize(float3(-dzdx, -dzdy, 1.0f));

    float3 rotatedNormal = localNormal;
    rotatedNormal.x = localNormal.x * cosYaw - localNormal.z * sinYaw;
    rotatedNormal.z = localNormal.x * sinYaw + localNormal.z * cosYaw;

    float4 worldPos = mul(float4(rotatedPos, 1.0f), inst.World);
    output.position = mul(mul(worldPos, View), Projection);

    output.prevClip = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (TemporalParams.y > 0.5f) {
        float3 prevLocal = FlagLocalPosition(input.position, input.texCoord,
                                             inst.Flags, TemporalParams.x);
        output.prevClip = TemporalPrevClip(
            mul(float4(prevLocal, 1.0f), inst.PrevWorld).xyz);
    }

    float3x3 world3x3 = (float3x3)inst.World;
    output.normal = normalize(mul(rotatedNormal, world3x3));
    output.texCoord = input.texCoord;
    output.color = input.color * inst.Color;

    return output;
}
