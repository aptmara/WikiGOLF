/**
 * @file TexturedPS.hlsl
 * @brief 看板および画像テクスチャ用ピクセルシェーダー
 */

Texture2D diffuseTexture : register(t0);   /**< 看板画像テクスチャ */
SamplerState samplerState : register(s0);  /**< サンプラーステート */

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 Pos : SV_POSITION;           /**< 射影座標 */
    float3 Normal : NORMAL;             /**< ワールド法線 */
    float2 TexCoord : TEXCOORD0;        /**< UV座標 */
    float4 Color : COLOR;               /**< マテリアルカラー（額縁色） */
    float FadeFactor : TEXCOORD1;       /**< 近接ディザフェード係数 (0～1) */
    float Time : TEXCOORD2;             /**< 経過時間[秒] */
    float EffectIntensity : TEXCOORD3;  /**< 枠演出強度 (0～1) */
};

/** @brief 4x4 Bayerディザ行列（近接時のドットフェード用） */
static const float kBayer4x4[16] = {
     0.0f / 16.0f,  8.0f / 16.0f,  2.0f / 16.0f, 10.0f / 16.0f,
    12.0f / 16.0f,  4.0f / 16.0f, 14.0f / 16.0f,  6.0f / 16.0f,
     3.0f / 16.0f, 11.0f / 16.0f,  1.0f / 16.0f,  9.0f / 16.0f,
    15.0f / 16.0f,  7.0f / 16.0f, 13.0f / 16.0f,  5.0f / 16.0f,
};

/** @brief UV空間における額縁ボーダー幅 */
static const float kFrameBorder = 0.07f;

/**
 * @brief 看板ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 描画カラー値
 */
float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.TexCoord;
    bool inFrame = uv.x < kFrameBorder || uv.x > 1.0f - kFrameBorder ||
                  uv.y < kFrameBorder || uv.y > 1.0f - kFrameBorder;

    float4 finalColor;
    if (inFrame) {
        float3 frameBase = input.Color.rgb;
        float intensity = input.EffectIntensity;

        // パルス（明滅）計算
        float pulseSpeed = lerp(1.0f, 4.0f, intensity);
        float pulseAmount = lerp(0.0f, 0.25f, intensity);
        float pulse = 1.0f + pulseAmount * sin(input.Time * pulseSpeed);

        // シマー（斜め光帯）計算
        float diag = (uv.x + uv.y) * 0.5f;
        float shimmerSpeed = lerp(0.05f, 0.7f, intensity);
        float shimmerPos = frac(diag - input.Time * shimmerSpeed);
        float band = smoothstep(0.0f, 0.12f, shimmerPos) *
                     (1.0f - smoothstep(0.12f, 0.28f, shimmerPos));
        float shimmer = band * intensity;

        float3 frameColor = frameBase * pulse + float3(1.0f, 1.0f, 1.0f) * shimmer * 0.6f;
        finalColor = float4(saturate(frameColor), 1.0f);
    } else {
        // 写真サンプリング（額縁内側へのリマップ）
        float2 innerUV = (uv - kFrameBorder) / (1.0f - 2.0f * kFrameBorder);
        float4 texColor = diffuseTexture.Sample(samplerState, innerUV);

        finalColor = texColor;
        // ガンマリフト
        finalColor.rgb = pow(saturate(finalColor.rgb), 0.85f);
        finalColor.a = texColor.a;
    }

    // Bayerディザクリッピングによる近接透過
    uint2 pixelCoord = uint2(input.Pos.xy) & 3;
    float threshold = kBayer4x4[pixelCoord.y * 4 + pixelCoord.x];
    clip(input.FadeFactor - threshold - 0.001f);

    return finalColor;
}
