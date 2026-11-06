/**
 * @file ArticlePaperPS.hlsl
 * @brief 記事の紙を照明なしで描くピクセルシェーダー（頂点シェーダーは TexturedVS）
 */

Texture2D articleTexture : register(t0);  /**< 記事のタイル */
SamplerState samplerState : register(s0); /**< サンプラーステート */

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力（TexturedVS.hlsl の出力と対応）
 */
struct PS_INPUT {
    float4 Pos : SV_POSITION;           /**< 射影座標 */
    float3 Normal : NORMAL;             /**< 未使用 */
    float2 TexCoord : TEXCOORD0;        /**< UV座標 */
    float4 Color : COLOR;               /**< 乗算カラー。aは不透明度 */
    float FadeFactor : TEXCOORD1;       /**< タイルの上から何割を使うか。0なら白紙 */
    float Time : TEXCOORD2;             /**< 未使用 */
    float EffectIntensity : TEXCOORD3;  /**< 紙の左右の余白（幅に対する割合） */
};

#include "TemporalVelocity.hlsli"

/**
 * @brief ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 記事の色と速度
 */
SceneOutput main(PS_INPUT input) {
    const float margin = input.EffectIntensity;
    const float u = (input.TexCoord.x - margin) / (1.0f - 2.0f * margin);

    float3 paper = float3(1.0f, 1.0f, 1.0f);
    if (input.FadeFactor > 0.0f && u >= 0.0f && u <= 1.0f) {
        // サンプラーはラップなので、継ぎ目で反対側の端を拾わないよう内側に留める
        float width, height;
        articleTexture.GetDimensions(width, height);
        const float2 halfTexel = 0.5f / float2(width, height);
        const float2 uv = clamp(float2(u, input.TexCoord.y * input.FadeFactor),
                                halfTexel, 1.0f - halfTexel);
        paper = articleTexture.Sample(samplerState, uv).rgb;
    }

    SceneOutput output;
    output.color = float4(paper * input.Color.rgb, input.Color.a);
    output.velocity = kCameraOnlyVelocity;
    return output;
}
