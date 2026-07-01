cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
    float2 worldXZ : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // Transform normal to View space
    float3 viewNormal = normalize(mul((float3x3)View, input.normal));
    
    // viewNormal.z is 1.0 at the center (facing camera) and 0.0 at the silhouette
    // We use absolute value because backfaces might also render depending on rasterizer state
    float centerGlow = saturate(abs(viewNormal.z));
    
    // Soft radial falloff
    float glow = pow(centerGlow, 3.0f);
    
    // Output glowing color. We multiply the RGB by some boost to make it look bright,
    // and fade out at the edges.
    return float4(MaterialColor.rgb, MaterialColor.a * glow);
}
