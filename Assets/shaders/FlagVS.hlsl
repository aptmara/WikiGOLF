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
};

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
    float windAmplitude = amplitude * (0.65f + speedFactor * 1.45f);

    float tipWeight = u * u * (3.0f - 2.0f * u);
    float freeEdge = smoothstep(0.62f, 1.0f, u);
    float verticalBend = 1.0f - abs(v - 0.5f) * 2.0f;

    float primary = sin(timePhase * 3.35f - u * 7.6f);
    float secondary = sin(timePhase * 6.20f - u * 13.5f + v * 2.4f);
    float edgeRipple = sin(timePhase * 9.8f - u * 22.0f) * freeEdge;

    float3 localPos = input.position;
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

    float3x3 world3x3 = (float3x3)inst.World;
    output.normal = normalize(mul(rotatedNormal, world3x3));
    output.texCoord = input.texCoord;
    output.color = input.color * inst.Color;

    return output;
}
