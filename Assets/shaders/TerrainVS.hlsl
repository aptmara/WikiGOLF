cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 MaterialFlags; // x: hasTexture, y: hasNormalMap (unused here)
    float4 LightDir; // w is unused
    float4 CameraPos; // w is unused
};

struct VS_INPUT {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BINORMAL;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output = (PS_INPUT)0;
    
    float4 pos = float4(input.Pos, 1.0f);
    float4 worldPos = mul(pos, World);
    output.WorldPos = worldPos.xyz;
    
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);
    
    // 法線の回転（スケーリングなしと仮定）
    output.Normal = mul(input.Normal, (float3x3)World);
    
    output.Tex = input.Tex;
    output.Color = input.Color * Color; // マテリアル色 * 頂点色
    
    return output;
}
