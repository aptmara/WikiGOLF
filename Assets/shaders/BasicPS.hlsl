/**
 * @file BasicPS.hlsl
 * @brief 基本ピクセルシェーダー（可読性向上機能付き）
 */

Texture2D diffuseTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState texSampler : register(s0);

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags; // x: hasDiffuse, y: hasNormalMap, z: uvScale, w: readabilityMode
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
    float hasDiffuse = MaterialFlags.x;
    float hasNormalMap = MaterialFlags.y;
    float readabilityMode = MaterialFlags.w;

    // ベースカラー
    float4 baseColor = input.color * MaterialColor;

    // UVスケール
    float uvScale = max(MaterialFlags.z, 1.0f);
    float2 uv = input.texCoord * uvScale;

    if (hasDiffuse > 0.5f) {
        float4 texColor = diffuseTexture.Sample(texSampler, uv);
        
        if (readabilityMode > 0.5f) {
            // 可読性向上モード: Wikiテキスト用
            // 輝度計算
            float lum = dot(texColor.rgb, float3(0.299, 0.587, 0.114));

            // 文字部分の検出（中間色以外）
            // 背景が白なら lum > 0.8, 背景が黒なら lum < 0.2
            // ここでは「背景色から離れている部分」を文字とみなす
            // シンプルに、コントラストを極端に強化する
            float contrast = 1.2f;
            float3 enhancedColor = saturate((texColor.rgb - 0.5f) * contrast + 0.5f);

            // アルファ値を輝度に応じて調整（文字をくっきりさせる）
            // 白背景・黒文字の場合：lumが低いほど文字
            // 黒背景・白文字の場合：lumが高いほど文字
            // 現状は乗算描画（白背景）に移行したため、それに合わせる

            if (lum < 0.9f) {
                // 文字または装飾部分
                baseColor.rgb = enhancedColor;
                baseColor.a = MaterialColor.a; // 指定された透明度を維持
            } else {
                // 背景部分（ほぼ白）
                baseColor.rgb = float3(1,1,1);
                baseColor.a = MaterialColor.a * 0.5f; // 背景はさらに薄く
            }
        }
 else {
            baseColor.rgb *= texColor.rgb;
            baseColor.a *= texColor.a;
        }
    }

    clip(baseColor.a - 0.001f);

    // ライティング (Overlay以外でのみ適用するのが望ましいが、現状は簡易的に)
    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(float3(0.5f, -1.0f, 0.5f));
    float diffuse = max(dot(normal, -lightDir), 0.0f);
    float ambient = 0.4f;
    float lighting = saturate(diffuse + ambient);
    
    // readabilityMode時はライティングを弱めにして文字を見やすくする
    if (readabilityMode > 0.5f) {
        lighting = lerp(lighting, 1.0f, 0.5f);
    }

    return float4(baseColor.rgb * lighting, baseColor.a);
}
