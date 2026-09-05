// TexturedPS.hlsl - テクスチャ付きピクセルシェーダー

Texture2D diffuseTexture : register(t0);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR;
    float FadeFactor : TEXCOORD1;
    float Time : TEXCOORD2;
    float EffectIntensity : TEXCOORD3;
};

// 4x4 Bayerディザ行列（近接時のドットフェード用）
static const float kBayer4x4[16] = {
     0.0f / 16.0f,  8.0f / 16.0f,  2.0f / 16.0f, 10.0f / 16.0f,
    12.0f / 16.0f,  4.0f / 16.0f, 14.0f / 16.0f,  6.0f / 16.0f,
     3.0f / 16.0f, 11.0f / 16.0f,  1.0f / 16.0f,  9.0f / 16.0f,
    15.0f / 16.0f,  7.0f / 16.0f, 13.0f / 16.0f,  5.0f / 16.0f,
};

// 実際のWikipedia記事写真は森・芝生など暗部の多いものも珍しくなく、
// トーンカーブ補正だけでは「看板として明るく見せる」のに限界がある
// （写真の内容そのものが暗いため）。そこで実物の看板同様、台紙
// （額縁）の上に写真を載せる構成にし、内容によらず視認性を確保する。
// 額縁の色はホップ数カラー（input.Color、GetHoleColorと同じ配色）を使い、
// EffectIntensity（目的地に近いほど強い）に応じてパルス＋シマーを重ねる。
static const float kFrameBorder = 0.07f; // UV空間での額縁の太さ

float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.TexCoord;
    bool inFrame = uv.x < kFrameBorder || uv.x > 1.0f - kFrameBorder ||
                  uv.y < kFrameBorder || uv.y > 1.0f - kFrameBorder;

    float4 finalColor;
    if (inFrame) {
        float3 frameBase = input.Color.rgb;
        float intensity = input.EffectIntensity;

        // パルス（明滅）。目的地に近いホールほど速く・大きく明滅する。
        float pulseSpeed = lerp(1.0f, 4.0f, intensity);
        float pulseAmount = lerp(0.0f, 0.25f, intensity);
        float pulse = 1.0f + pulseAmount * sin(input.Time * pulseSpeed);

        // 斜めに流れるシマー（光の帯）。目的地に近いほど速く流れる。
        float diag = (uv.x + uv.y) * 0.5f;
        float shimmerSpeed = lerp(0.05f, 0.7f, intensity);
        float shimmerPos = frac(diag - input.Time * shimmerSpeed);
        float band = smoothstep(0.0f, 0.12f, shimmerPos) *
                     (1.0f - smoothstep(0.12f, 0.28f, shimmerPos));
        float shimmer = band * intensity;

        float3 frameColor = frameBase * pulse + float3(1.0f, 1.0f, 1.0f) * shimmer * 0.6f;
        finalColor = float4(saturate(frameColor), 1.0f);
    } else {
        // 額縁の内側へ写真をリマップしてサンプリング
        float2 innerUV = (uv - kFrameBorder) / (1.0f - 2.0f * kFrameBorder);
        float4 texColor = diffuseTexture.Sample(samplerState, innerUV);

        // アンリット（無灯）表示。写真自体はホップ数カラーで着色せず、
        // ありのままの色で見せる（着色は額縁側だけ）。
        // 看板はビルボードのため法線が常に変化し、方向性ライティングを掛けると
        // 光源方向とのなす角によっては最低輝度まで暗く沈んでしまうため、
        // ライティングもかけない。
        finalColor = texColor;

        // 画面全体のポストプロセス（コントラスト・彩度・霧）を通っても
        // 極端に沈んで見えないよう、軽くガンマリフトしておく。
        finalColor.rgb = pow(saturate(finalColor.rgb), 0.85f);
        finalColor.a = texColor.a;
    }

    // 近づきすぎた場合はドットパターン（Bayerディザ）で段階的に消す。
    // アルファブレンドではなくclipを使うため、ソート順の問題が起きない。
    uint2 pixelCoord = uint2(input.Pos.xy) & 3;
    float threshold = kBayer4x4[pixelCoord.y * 4 + pixelCoord.x];
    clip(input.FadeFactor - threshold - 0.001f);

    return finalColor;
}
