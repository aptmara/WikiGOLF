/**
 * @file BasicVS.hlsl
 * @brief 基本頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags; // x: hasDiffuse, y: hasNormalMap
};

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    float4 worldPos = mul(float4(input.position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);
    
    float3x3 world3x3 = (float3x3)World;
    output.normal = mul(input.normal, world3x3);
    output.tangent = mul(input.tangent, world3x3);
    output.bitangent = mul(input.bitangent, world3x3);
    output.texCoord = input.texCoord;
    output.color = input.color * MaterialColor;
    
    return output;
}
