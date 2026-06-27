/**
 * @file ParticlePS.hlsl
 * @brief パーティクル専用ピクセルシェーダー
 * 
 * テクスチャなしでもソフトな円形パーティクルを描画し、
 * 発光感（Emissive）のある表現をサポートします。
 */

Texture2D diffuseTexture : register(t0);
SamplerState texSampler : register(s0);

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags; // x: hasDiffuse, z: isDust, w: denseCore/star
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // 頂点カラー * マテリアルカラー
    float4 finalColor = input.color;

    // UV座標から中心(0.5, 0.5)への距離を計算
    // これにより、テクスチャがなくても円形のソフトパーティクルが作れる
    float2 centerDist = input.texCoord - float2(0.5, 0.5);
    float d = length(centerDist) * 2.0; // 0.0(中心) ~ 1.0(端)
    
    // 円形の減衰（中心ほど明るい）
    float alpha = saturate(1.0 - d);
    // 指数関数的に減衰させることでよりソフトに
    alpha = pow(alpha, 2.0);

    // バンカー砂煙などの「塊」感を出すための調整
    // RenderSystemにより、customFlags.x は MaterialFlags.z に渡される
    if (MaterialFlags.z > 0.5) { 
        float core = pow(saturate(1.0 - d * 0.78), 1.35);
        float fleck = frac(sin(dot(input.texCoord * 38.0, float2(12.9898, 78.233))) * 43758.5453);
        float grain = lerp(0.82, 1.16, fleck);
        alpha = saturate(max(alpha * 1.55, core * 0.68) * grain);
        if (MaterialFlags.w > 0.5) {
            alpha = saturate(max(alpha, core * 0.82));
        }
    } else if (MaterialFlags.w > 0.5) {
        float2 p = abs(centerDist * 2.0);
        float diamond = saturate(1.0 - (p.x + p.y));
        float cross = saturate(1.0 - min(p.x, p.y) * 5.2) * saturate(1.0 - max(p.x, p.y) * 1.1);
        float diagonal = saturate(1.0 - abs(p.x - p.y) * 5.5) * saturate(1.0 - max(p.x, p.y) * 1.35);
        float core = saturate(1.0 - d * 3.0);
        alpha = saturate(pow(max(diamond, max(cross * 0.72, diagonal * 0.45)), 1.25) + core * 0.85);
    }

    // パーティクルはライティングを無視するか、非常に簡易的なものにする（発光表現）
    // ここでは自己発光として扱う
    
    return float4(finalColor.rgb, finalColor.a * alpha);
}
