/**
 * @file GrassFieldPS.hlsl
 * @brief 手続き的に生やした芝の葉の照明（両面の拡散光、透過光、弱い光沢、フォグ）
 */

#include "GrassFieldCommon.hlsli"
#include "ShadowSampling.hlsli"
#include "TemporalVelocity.hlsli"

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;          // a: 根本からの高さの割合
    float3 worldPos : TEXCOORD1;
    float materialClass : TEXCOORD3;
    float4 shadowPosition : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
};

/**
 * @brief 芝の影（比較サンプラーの 2x2 補間を1回だけ読む）
 * @details 葉は細く数が多いので、ShadowSampling.hlsli の 3x3 のぼかしは使わない。
 */
float SampleGrassShadow(float4 shadowPosition, float3 normal) {
    if (GrassShadowParams.x < 0.5f || shadowPosition.w <= 0.0f) {
        return 1.0f;
    }
    float3 projected = shadowPosition.xyz / shadowPosition.w;
    float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
    if (projected.z <= 0.0f || projected.z >= 1.0f || any(uv < 0.0f) || any(uv > 1.0f)) {
        return 1.0f;
    }
    float normalLight = saturate(dot(normal, normalize(-GrassLight.xyz)));
    float bias = lerp(0.0018f, 0.00045f, normalLight);
    float visibility = g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler, uv, projected.z - bias);
    return lerp(0.38f, 1.0f, visibility);
}

[earlydepthstencil]
SceneOutput main(PS_INPUT input) {
    float tipWeight = saturate(input.color.a);

    float3 N = normalize(input.normal);
    float3 L = normalize(-GrassLight.xyz);
    float3 V = normalize(GrassCamera.xyz - input.worldPos);

    // 葉は薄いので、表裏どちらから光を受けても暗くしない
    float nl = dot(N, L);
    float diffuse = abs(nl);
    float wrap = saturate(abs(nl) * 0.65f + 0.35f);

    // 太陽を透かして葉先が明るくなる
    float backlight = pow(saturate(dot(-L, V)), 3.0f) * tipWeight * 0.38f *
                      lerp(0.45f, 1.0f, input.materialClass);

    // 根本は周りの葉に遮られて暗い
    float rootOcclusion = lerp(0.72f, 0.54f, input.materialClass);
    float ao = lerp(rootOcclusion, 1.0f, tipWeight * tipWeight);

    float3 ambient = float3(0.27f, 0.31f, 0.24f) * ao;
    float3 lightColor = float3(1.0f, 0.97f, 0.88f);
    float shadow = SampleGrassShadow(input.shadowPosition, N);
    float3 lit = ambient + lightColor * lerp(wrap, diffuse, 0.58f) * ao * shadow;

    float3 baseColor = input.color.rgb;
    float3 finalColor = baseColor * lit + lightColor * backlight * baseColor;

    float3 H = normalize(L + V);
    float leafSheen = pow(saturate(abs(dot(N, H))), 32.0f) *
                      lerp(0.20f, 0.10f, input.materialClass) * (0.72f + tipWeight * 0.28f);
    finalColor += lightColor * leafSheen * baseColor;

    // 地形と同じフォグ（GrassCamera.w はマップビュー中 0）
    float dist = distance(GrassCamera.xyz, input.worldPos);
    float fogFactor = saturate((dist - 60.0f) / (400.0f - 60.0f)) * GrassCamera.w;
    finalColor = lerp(finalColor, float3(0.75f, 0.85f, 0.95f), fogFactor);

    return MakeSceneOutput(float4(finalColor, 1.0f),
                           EncodeVelocity(input.position, input.prevClip));
}
