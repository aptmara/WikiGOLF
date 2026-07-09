/**
 * @file BasicVS.hlsl
 * @brief 基本頂点シェーダー（インスタンシング対応）
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
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
    uint instanceID : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
    float2 worldXZ : TEXCOORD1;
    float4 materialFlags : TEXCOORD4;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    InstanceData inst = g_instances[input.instanceID];
    
    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);
    
    float3x3 world3x3 = (float3x3)inst.World;
    output.normal = mul(input.normal, world3x3);
    output.tangent = mul(input.tangent, world3x3);
    output.bitangent = mul(input.bitangent, world3x3);
    output.texCoord = input.texCoord;
    
    float4 vcolor = (input.color.a <= 0.0001f && all(input.color.rgb == 0)) ?
                    float4(1,1,1,1) : input.color;
    output.color = vcolor * inst.Color;

    output.worldXZ = worldPos.xz;
    output.materialFlags = inst.Flags;
    output.materialFlags.w = inst.Color.a; // パラメータ引き渡し用の元のマテリアルアルファ
    
    return output;
}
