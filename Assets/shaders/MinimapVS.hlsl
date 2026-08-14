/**
 * @file MinimapVS.hlsl
 * @brief ミニマップ描画専用のインスタンシング対応頂点シェーダー
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
    float4 Flags; // x: hasTexture, y: forceOpaque, z: uvScale, w: unused
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
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float4 flags : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    InstanceData inst = g_instances[input.instanceID];

    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);
    float4 viewPos = mul(worldPos, View);
    output.position = mul(viewPos, Projection);

    output.texCoord = input.texCoord;

    float4 vertexColor = input.color;
    if (vertexColor.a <= 0.0001f && all(vertexColor.rgb == 0.0f)) {
        vertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    output.color = vertexColor * inst.Color;
    output.flags = inst.Flags;

    return output;
}
