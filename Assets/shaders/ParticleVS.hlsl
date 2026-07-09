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
    
    float globalTime = LightDir.w;
    float phase = inst.Flags.x;
    float amplitude = inst.Flags.y;
    
    float orbit = (globalTime + phase) * (0.75f + amplitude * 0.45f);
    float radius = 0.25f + 0.22f * amplitude;
    
    float3 offset = float3(0, 0, 0);
    offset.x = cos(orbit) * radius;
    offset.z = sin(orbit * 0.9f) * radius;
    offset.y = 0.18f * sin(orbit * 1.7f);
    
    float pulse = 0.5f + 0.5f * sin((globalTime + phase) * 3.3f);
    float scaleFactor = 0.75f + pulse * 0.55f;
    
    float3 localPos = input.position * scaleFactor;
    
    float4 worldPos = mul(float4(localPos, 1.0f), inst.World);
    worldPos.xyz += offset;
    
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);
    
    output.normal = mul(input.normal, (float3x3)inst.World);
    output.texCoord = input.texCoord;
    
    output.color = input.color * inst.Color;
    output.color.a = 0.16f + 0.26f * pulse * amplitude;
    
    output.materialFlags = inst.Flags;
    
    return output;
}
