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
    float4 MaterialFlags; // x: hasDiffuse, y: isBunker (special dust effect)
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
        alpha = saturate(alpha * 1.5);
    }

    // パーティクルはライティングを無視するか、非常に簡易的なものにする（発光表現）
    // ここでは自己発光として扱う
    
    return float4(finalColor.rgb, finalColor.a * alpha);
}
