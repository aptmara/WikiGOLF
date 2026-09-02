/**
 * @file GrassPS.hlsl
 * @brief 管理されたラフ芝のピクセルシェーダー。
 *        薄い葉の両面照明、葉面の弱い光沢、透過光、距離フェードを扱う。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
    float4 LightDir;
    float4 CameraPos;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float distanceFade : TEXCOORD2;
    float materialClass : TEXCOORD3;
};

float InterleavedGradientNoise(float2 pixelPosition) {
    return frac(52.9829189f * frac(dot(pixelPosition,
                                      float2(0.06711056f, 0.00583715f))));
}

float4 main(PS_INPUT input) : SV_TARGET {
    clip(input.distanceFade - InterleavedGradientNoise(input.position.xy));

    float tipWeight = saturate(1.0f - input.texCoord.y); // 根本=0, 穂先=1

    float3 N = normalize(input.normal);
    float3 L = normalize(-LightDir.xyz);
    float3 V = normalize(CameraPos.xyz - input.worldPos);

    // 葉は非常に薄いため、表裏で同じ法線を持つ板として暗転させず、
    // 両面から受ける拡散光として評価する。
    float nl = dot(N, L);
    float diffuse = abs(nl);
    float wrap = saturate(abs(nl) * 0.65f + 0.35f);

    // 太陽を透かして穂先が明るくなる疑似的な半透過（Subsurface風）。
    float backlight = pow(saturate(dot(-L, V)), 3.0f) * tipWeight * 0.38f *
                      lerp(0.45f, 1.0f, input.materialClass);

    // 根本は地面に接しているため影になりやすい（疑似AO）。深く落として
    // 個々の葉が土から浮いて見えず、地面と一体化した芝生の質感にする。
    float rootOcclusion = lerp(0.72f, 0.54f, input.materialClass);
    float ao = lerp(rootOcclusion, 1.0f, tipWeight * tipWeight);

    float3 ambient = float3(0.27f, 0.31f, 0.24f) * ao;
    float3 lightColor = float3(1.0f, 0.97f, 0.88f);
    float3 lit = ambient + lightColor * lerp(wrap, diffuse, 0.58f) * ao;

    float3 baseColor = input.color.rgb;
    float3 finalColor = baseColor * lit + lightColor * backlight * baseColor;

    // 刈り込まれた葉の細い面にだけ出る控えめな葉面光沢。
    float3 H = normalize(L + V);
    float leafSheen = pow(saturate(abs(dot(N, H))), 32.0f) *
                      lerp(0.20f, 0.10f, input.materialClass) *
                      (0.72f + tipWeight * 0.28f);
    finalColor += lightColor * leafSheen * baseColor;

    // 地形と統一したフォグ
    // CameraPos.w はマップビュー時に0(フォグ無効)、通常時は1になる
    // (RenderSystem::camPos参照)。俯瞰視点では高高度からの距離が
    // フォグ終了距離をすぐ超え画面が白く覆われてしまうため無効化する。
    float dist = distance(CameraPos.xyz, input.worldPos);
    float fogStart = 60.0f;
    float fogEnd = 400.0f;
    float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart)) * CameraPos.w;
    float3 fogColor = float3(0.75f, 0.85f, 0.95f);
    finalColor = lerp(finalColor, fogColor, fogFactor);

    return float4(finalColor, 1.0f);
}
