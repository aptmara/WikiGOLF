/**
 * @file FlagPS.hlsl
 * @brief プロシージャル旗布用ピクセルシェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags;
    float4 LightDir;
    float4 CameraPos;
};

static const float kDither4x4[16] = {
    1.0f / 32.0f, 17.0f / 32.0f, 5.0f / 32.0f, 21.0f / 32.0f,
    25.0f / 32.0f, 9.0f / 32.0f, 29.0f / 32.0f, 13.0f / 32.0f,
    7.0f / 32.0f, 23.0f / 32.0f, 3.0f / 32.0f, 19.0f / 32.0f,
    31.0f / 32.0f, 15.0f / 32.0f, 27.0f / 32.0f, 11.0f / 32.0f
};

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 position : SV_POSITION; /**< 射影座標 */
    float3 normal : NORMAL;        /**< ワールド法線 */
    float2 texCoord : TEXCOORD;    /**< UV座標 */
    float4 color : COLOR;          /**< 頂点カラー */
    float4 prevClip : TEXCOORD1;   /**< 前フレームのクリップ座標 */
};

#include "TemporalVelocity.hlsli"

/**
 * @brief 旗布ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 布地陰影計算済みピクセルカラーと速度
 */
SceneOutput main(PS_INPUT input) {
    uint2 ditherCoord = uint2(input.position.xy) & 3;
    float ditherThreshold = kDither4x4[ditherCoord.y * 4 + ditherCoord.x];
    clip(input.color.a - ditherThreshold);

    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(float3(0.45f, -1.0f, 0.35f));
    float diffuse = abs(dot(normal, -lightDir));
    float fabricWeave = sin(input.texCoord.x * 95.0f) * 0.018f +
                        sin(input.texCoord.y * 72.0f) * 0.014f;
    float edgeShade = lerp(0.92f, 1.05f, saturate(input.texCoord.x));
    float lighting = saturate(0.48f + diffuse * 0.54f + fabricWeave) * edgeShade;
    return MakeSceneOutput(float4(saturate(input.color.rgb * lighting), input.color.a),
                           EncodeVelocity(input.position, input.prevClip));
}
