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

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(float3(0.45f, -1.0f, 0.35f));
    float diffuse = abs(dot(normal, -lightDir));
    float fabricWeave = sin(input.texCoord.x * 95.0f) * 0.018f +
                        sin(input.texCoord.y * 72.0f) * 0.014f;
    float edgeShade = lerp(0.92f, 1.05f, saturate(input.texCoord.x));
    float lighting = saturate(0.48f + diffuse * 0.54f + fabricWeave) * edgeShade;
    return float4(saturate(input.color.rgb * lighting), input.color.a);
}
