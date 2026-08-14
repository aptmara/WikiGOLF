/**
 * @file MinimapPS.hlsl
 * @brief ミニマップ描画専用の軽量ピクセルシェーダー（ノイズ・ライティングなし）
 */

Texture2D diffuseTexture : register(t0);
SamplerState texSampler : register(s0);

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float4 flags : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 baseColor = input.color;

    if (input.flags.x > 0.5f) {
        float uvScale = max(input.flags.z, 1.0f);
        float4 texColor = diffuseTexture.Sample(texSampler, input.texCoord * uvScale);
        baseColor.rgb *= texColor.rgb;
        baseColor.a *= texColor.a;
    }

    // 地形頂点のalphaには素材IDが格納されているため、透明度として扱わない。
    // 記事オーバーレイだけはテクスチャalphaを維持して下の地形を透過させる。
    if (input.flags.y > 0.5f) {
        baseColor.a = 1.0f;
    }

    clip(baseColor.a - 0.02f);
    return baseColor;
}
