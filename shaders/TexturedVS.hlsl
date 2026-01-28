// TexturedVS.hlsl - テクスチャ付きバーテックスシェーダー

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 MaterialFlags; // x: hasTexture, y: hasNormalMap (unused here)
};

struct VS_INPUT {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BINORMAL;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    
    float4 worldPos = mul(float4(input.Pos, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Pos = mul(viewPos, Projection);
    
    output.Normal = mul(input.Normal, (float3x3)World);
    output.TexCoord = input.TexCoord;
    output.Color = Color;
    
    return output;
}
