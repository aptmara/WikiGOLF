#ifndef WIKIGOLF_SHADOW_SAMPLING_HLSLI
#define WIKIGOLF_SHADOW_SAMPLING_HLSLI

Texture2D<float> g_ShadowMap : register(t14);
SamplerComparisonState g_ShadowSampler : register(s1);

float SampleShadow(float4 shadowPosition, float3 normal, float3 lightDirection,
                   float enabled) {
    if (enabled < 0.5f || shadowPosition.w <= 0.0f) {
        return 1.0f;
    }

    float3 projected = shadowPosition.xyz / shadowPosition.w;
    float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
    if (projected.z <= 0.0f || projected.z >= 1.0f ||
        any(uv < 0.0f) || any(uv > 1.0f)) {
        return 1.0f;
    }

    float normalLight = saturate(dot(normalize(normal),
                                     normalize(-lightDirection)));
    float bias = lerp(0.0018f, 0.00045f, normalLight);
    const float2 texelSize = 1.0f / 2048.0f;
    float visibility = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            visibility += g_ShadowMap.SampleCmpLevelZero(
                g_ShadowSampler, uv + float2(x, y) * texelSize,
                projected.z - bias);
        }
    }
    return lerp(0.38f, 1.0f, visibility / 9.0f);
}

#endif
