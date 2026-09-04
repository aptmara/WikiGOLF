/**
 * @file TrailVS.hlsl
 * @brief ワールド座標へ固定するボールトレイル専用頂点シェーダー
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
};

struct InstanceData {
    matrix World;
    float4 Color;
    float4 Flags;
};

StructuredBuffer<InstanceData> g_instances : register(t15);

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    uint instanceID : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float4 materialFlags : TEXCOORD4;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    InstanceData inst = g_instances[input.instanceID];

    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = mul(input.normal, (float3x3)inst.World);
    output.texCoord = input.texCoord;
    output.color = input.color * inst.Color;
    output.materialFlags = inst.Flags;
    return output;
}
