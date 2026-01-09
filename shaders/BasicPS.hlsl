/**
 * @file BasicPS.hlsl
 * @brief 基本ピクセルシェーダー（テクスチャ対応）
 */

Texture2D diffuseTexture : register(t0);
SamplerState texSampler : register(s0);

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // テクスチャサンプリング
    float4 texColor = diffuseTexture.Sample(texSampler, input.texCoord);
    
    // テクスチャが無い場合（黒 or 透明）はマテリアルカラーを使用
    float4 baseColor = input.color;
    if (texColor.a > 0.01f) {
        baseColor = texColor;
    }
    
    // シンプルなディレクショナルライティング
    float3 lightDir = normalize(float3(0.5f, -1.0f, 0.5f));
    float3 normal = normalize(input.normal);
    
    float diffuse = max(dot(normal, -lightDir), 0.0f);
    float ambient = 0.3f;
    float lighting = saturate(diffuse + ambient);
    
    return float4(baseColor.rgb * lighting, baseColor.a);
}

