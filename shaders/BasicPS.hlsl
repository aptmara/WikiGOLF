/**
 * @file BasicPS.hlsl
 * @brief 基本ピクセルシェーダー（テクスチャ対応）
 */

Texture2D diffuseTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState texSampler : register(s0);

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags; // x: hasDiffuse, y: hasNormalMap
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float hasDiffuse = MaterialFlags.x;
    float hasNormalMap = MaterialFlags.y;

    // テクスチャサンプリング
    // マテリアルカラーを乗算
    float4 baseColor = input.color * MaterialColor;

    // アルファテスト: 透明度が高いピクセルは描画しない（Zバッファへの書き込み防止）
    clip(baseColor.a - 0.05f);

    if (hasDiffuse > 0.5f) {
        float4 texColor = diffuseTexture.Sample(texSampler, input.texCoord);
        baseColor *= texColor;
    }
    
    // 法線（必要ならノーマルマップで置換）
    float3 normal = normalize(input.normal);
    if (hasNormalMap > 0.5f) {
        float3 t = normalize(input.tangent);
        float3 b = normalize(input.bitangent);
        float3 n = normal;

        // オルソ化（非単位スケール行列対策）
        t = normalize(t - n * dot(n, t));
        b = normalize(b - n * dot(n, b));
        if (all(abs(t) < 1e-4)) {
            t = normalize(cross(float3(0, 1, 0), n));
        }
        if (all(abs(b) < 1e-4)) {
            b = normalize(cross(n, t));
        }

        float3 mapN = normalTexture.Sample(texSampler, input.texCoord).xyz;
        mapN = mapN * 2.0f - 1.0f;
        float3x3 TBN = float3x3(t, b, n);
        normal = normalize(mul(mapN, TBN));
    }

    // シンプルなディレクショナルライティング
    float3 lightDir = normalize(float3(0.5f, -1.0f, 0.5f));
    
    float diffuse = max(dot(normal, -lightDir), 0.0f);
    float ambient = 0.3f;
    float lighting = saturate(diffuse + ambient);
    
    return float4(baseColor.rgb * lighting, baseColor.a);
}

