/**
 * @file BasicVS.hlsl
 * @brief 基本頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
};

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    float4 worldPos = mul(float4(input.position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);
    
    output.normal = mul(input.normal, (float3x3)World);
    output.texCoord = input.texCoord;
    output.texCoord = input.texCoord;
    output.color = input.color * MaterialColor;
    
    return output;
}
