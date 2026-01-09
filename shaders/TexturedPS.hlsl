// TexturedPS.hlsl - テクスチャ付きピクセルシェーダー

Texture2D diffuseTexture : register(t0);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // テクスチャサンプリング
    float4 texColor = diffuseTexture.Sample(samplerState, input.TexCoord);
    
    // 簡易ライティング
    float3 lightDir = normalize(float3(0.3f, 1.0f, -0.5f));
    float nDotL = max(0.3f, dot(normalize(input.Normal), lightDir));
    
    float4 finalColor = texColor * nDotL;
    finalColor.a = texColor.a;
    
    return finalColor;
}
