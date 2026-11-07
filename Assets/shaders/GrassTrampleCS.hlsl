/**
 * @file GrassTrampleCS.hlsl
 * @brief ボールが転がった跡に芝を押し倒し、時間とともに起き上がらせる
 * @details テクスチャはワールドに固定し、端で巻き戻す（16m 四方、1テクセル 6.25cm）。
 *          窓が動いて新しく入ったテクセルは 0 から始める。
 *          出力 rg: 倒れる向き * 0.5 + 0.5、b: 倒れ具合
 */

#include "GrassFieldCommon.hlsli"

Texture2D<float4> g_PreviousTrample : register(t0);
RWTexture2D<float4> g_Trample : register(u0);

/** @brief 巻き戻すテクセル番号 i が、原点 originTexel の窓で表すワールドのテクセル座標 */
float2 WorldTexelOf(float2 texel, float2 originTexel) {
    float2 wrapped = texel - fmod(originTexel, kTrampleResolution);
    wrapped = wrapped - floor(wrapped / kTrampleResolution) * kTrampleResolution;
    return originTexel + wrapped;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float2 originTexel = floor(GrassTrampleOrigin.xy / kTrampleTexelSize + 0.5f);
    float2 prevOriginTexel = floor(GrassTrampleOrigin.zw / kTrampleTexelSize + 0.5f);
    float2 worldTexel = WorldTexelOf(float2(id.xy), originTexel);

    float4 previous = g_PreviousTrample.Load(int3(id.xy, 0));
    float2 relative = worldTexel - prevOriginTexel;
    bool wasInside = all(relative >= 0.0f) && all(relative < kTrampleResolution);
    float amount = wasInside ? previous.b * GrassTrampleMotion.z : 0.0f;
    float2 direction = wasInside ? previous.rg * 2.0f - 1.0f : float2(0.0f, 0.0f);

    if (GrassTrampleBall.w > 0.5f) {
        float2 position = (worldTexel + 0.5f) * kTrampleTexelSize;
        float2 a = GrassTrampleBallPrev.xz;
        float2 b = GrassTrampleBall.xz;
        float2 ab = b - a;
        float t = saturate(dot(position - a, ab) / max(dot(ab, ab), 1.0e-6f));
        float2 closest = a + ab * t;
        float2 away = position - closest;
        float distanceToPath = length(away);
        float radius = GrassTrampleBallPrev.w;
        float falloff = saturate(1.0f - distanceToPath / radius);
        falloff = falloff * falloff * (3.0f - 2.0f * falloff);

        float2 velocity = GrassTrampleMotion.xy;
        float speed = length(velocity);
        float2 forward = speed > 0.01f ? velocity / speed : float2(0.0f, 0.0f);
        float2 outward = distanceToPath > 1.0e-4f ? away / distanceToPath : float2(0.0f, 0.0f);
        float2 push = forward * 0.75f + outward * 0.55f;
        float pushLength = length(push);
        push = pushLength > 1.0e-4f ? push / pushLength : float2(0.0f, 0.0f);

        float response = falloff * saturate(0.68f + speed * 0.045f);
        if (response > amount) {
            float blend = saturate(response - amount);
            direction = normalize(lerp(direction, push, blend) + float2(1.0e-5f, 0.0f));
            amount = response;
        }
    }

    g_Trample[id.xy] = float4(direction * 0.5f + 0.5f, amount, 1.0f);
}
