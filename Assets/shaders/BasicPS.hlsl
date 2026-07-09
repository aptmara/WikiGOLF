/**
 * @file BasicPS.hlsl
 * @brief 基本ピクセルシェーダー（テクスチャ対応）
 */

Texture2D diffuseTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState texSampler : register(s0);

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
};

// シンプルな擬似ノイズ
float hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float noise2d(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash21(i);
    float b = hash21(i + float2(1, 0));
    float c = hash21(i + float2(0, 1));
    float d = hash21(i + float2(1, 1));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p) {
    float v = 0.0;
    float a = 0.5;
    float2 shift = float2(37.0, 17.0);
    for (int i = 0; i < 4; ++i) {
        v += a * noise2d(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
    float2 worldXZ : TEXCOORD1;
    float4 materialFlags : TEXCOORD4;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float hasDiffuse = input.materialFlags.x;
    float hasNormalMap = input.materialFlags.y;

    // テクスチャサンプリング
    // マテリアルカラーを乗算
    float4 baseColor = input.color;

    // UVスケール (MaterialFlags.z = customFlags.x) を適用。未設定時は 1.0f
    float uvScale = max(input.materialFlags.z, 1.0f);
    float2 uv = input.texCoord * uvScale;

    // 頂点アルファはマテリアルIDとして使っているので、色のアルファからは取り除く
    baseColor.a = input.materialFlags.w;

    if (hasDiffuse > 0.5f) {
        float4 texColor = diffuseTexture.Sample(texSampler, uv);
        baseColor.rgb *= texColor.rgb;
        baseColor.a *= texColor.a;
    }

    // アルファテスト（地形用はほぼ不透明なので緩めのスレッショルド）
    clip(baseColor.a - 0.02f);

    // マテリアル種別をアルファから推定 (0:Fairway,1:Rough,2:Bunker,3:Green)
    float matId = floor(input.color.a * 4.0f + 0.5f);
    float2 wxz = input.worldXZ * 0.4f;
    float texNoise = fbm(wxz * 0.8f);

    // プロシージャルディテール（より自然なノイズベース）
    float shade = 0.0f;
    if (matId < 0.5f) { // Fairway
        shade = (fbm(wxz * 1.1f) - 0.5f) * 0.08f + (texNoise - 0.5f) * 0.05f;
    } else if (matId < 1.5f) { // Rough
        shade = (fbm(wxz * 2.5f) - 0.5f) * 0.15f;
    } else if (matId < 2.5f) { // Bunker
        shade = (fbm(wxz * 1.5f + float2(5.0, 3.0)) - 0.5f) * 0.12f;
    } else { // Green
        shade = (fbm(wxz * 2.0f) - 0.5f) * 0.07f;
    }
    baseColor.rgb = saturate(baseColor.rgb * (1.0f + shade));

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

        float3 mapN = normalTexture.Sample(texSampler, uv).xyz;
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
