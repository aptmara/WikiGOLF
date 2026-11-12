/**
 * @file ShotGuidePS.hlsl
 * @brief ショット予測表示（予測線・着地点の印・足元の向き表示）専用のピクセルシェーダー
 * @details 形はすべて式で描く（テクスチャ無し）。白い記事の地面や明るい空の上でも
 *          読めるよう、どの形も「色の本体」の下に「暗い縁」を敷く。
 *          番号は src/game/utils/ShotGuideStyle.h の shape と揃える。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
    float4 LightDir; /**< w: 経過時間[秒] */
};

#include "TemporalVelocity.hlsli"

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;     /**< x: 幅方向 0〜1、y: 長さ方向 0〜1（前が1） */
    float4 color : COLOR;
    float4 shape : TEXCOORD1;  /**< x: 形の番号、y: 形ごとの値、z: 道のり[m]、w: 実際の幅[m] */
};

static const float kTwoPi = 6.2831853;
static const float3 kRimColor = float3(0.02, 0.05, 0.09);

/** @brief 上の層を下の層へ重ねる（どちらも乗算前のアルファ） */
float4 Over(float4 top, float4 bottom) {
    float alpha = top.a + bottom.a * (1.0 - top.a);
    float3 rgb = (top.rgb * top.a + bottom.rgb * bottom.a * (1.0 - top.a)) /
                 max(alpha, 0.0001);
    return float4(rgb, alpha);
}

/** @brief 半径 radius・太さ thickness の輪 */
float Band(float r, float radius, float thickness, float aa) {
    return 1.0 - smoothstep(thickness - aa, thickness + aa, abs(r - radius));
}

/** @brief 色の輪の下に暗い縁を敷いたもの */
float4 RimmedBand(float r, float radius, float thickness, float aa, float3 color, float alpha) {
    float4 body = float4(color, Band(r, radius, thickness, aa) * alpha);
    float4 rim = float4(kRimColor, Band(r, radius, thickness * 1.9 + aa, aa) * alpha * 0.5);
    return Over(body, rim);
}

/** @brief 飛行区間の帯。光のかたまりが前へ流れる */
float4 RibbonFlight(PS_INPUT input) {
    float a = abs(input.uv.x * 2.0 - 1.0);
    float aa = fwidth(a);
    float body = 1.0 - smoothstep(0.40 - aa, 0.40 + aa, a);
    float rim = 1.0 - smoothstep(0.58 - aa, 0.58 + aa, a);
    float halo = pow(saturate(1.0 - a), 2.0);
    float core = 1.0 - smoothstep(0.0, 0.24, a);

    const float period = 4.0;
    float comet = pow(frac(input.shape.z / period), 5.0);
    // 遠くで模様が数ピクセルに潰れるところは、ちらつかないよう一様な明るさへ寄せる
    float pixelsPerPeriod = period / max(fwidth(input.shape.z), 0.00001);
    float flow = lerp(0.30, comet, saturate((pixelsPerPeriod - 8.0) / 16.0));

    float3 bodyColor = lerp(input.color.rgb, float3(1, 1, 1), saturate(core * 0.5 + flow * 0.8));
    float4 result = float4(input.color.rgb, halo * (0.16 + 0.34 * flow));
    result = Over(float4(kRimColor, rim * 0.50), result);
    result = Over(float4(bodyColor, body * (0.72 + 0.28 * flow)), result);
    return result;
}

/** @brief 転がり区間の帯。丸い点が並んで前へ流れる */
float4 RibbonRoll(PS_INPUT input) {
    const float period = 0.34;
    float width = input.shape.w;
    float2 p = float2((input.uv.x - 0.5) * width,
                      (frac(input.shape.z / period) - 0.5) * period);
    float r = length(p);
    float aa = fwidth(input.uv.x) * width;
    float radius = min(width * 0.20, period * 0.34);
    float dotBody = 1.0 - smoothstep(radius - aa, radius + aa, r);
    float dotRim = 1.0 - smoothstep(radius * 1.5 - aa, radius * 1.5 + aa, r);

    // 遠くで点が潰れるところは細い実線にする
    float a = abs(input.uv.x * 2.0 - 1.0);
    float lineAa = fwidth(a);
    float lineBody = 1.0 - smoothstep(0.30 - lineAa, 0.30 + lineAa, a);
    float lineRim = 1.0 - smoothstep(0.48 - lineAa, 0.48 + lineAa, a);
    float pixelsPerPeriod = period / max(fwidth(input.shape.z), 0.00001);
    float detail = saturate((pixelsPerPeriod - 5.0) / 6.0);

    float body = lerp(lineBody * 0.62, dotBody, detail);
    float rim = lerp(lineRim * 0.62, dotRim, detail);
    float3 bodyColor = lerp(input.color.rgb, float3(1, 1, 1), 0.25);
    return Over(float4(bodyColor, body), float4(kRimColor, rim * 0.5));
}

/** @brief 道すじを走る光の玉 */
float4 Orb(PS_INPUT input) {
    float r = length(input.uv - 0.5) * 2.0;
    float aa = fwidth(r);
    float core = 1.0 - smoothstep(0.34 - aa, 0.34 + aa, r);
    float rim = 1.0 - smoothstep(0.44 - aa, 0.44 + aa, r);
    float glow = pow(saturate(1.0 - r), 2.2);
    float twinkle = 0.88 + 0.12 * sin(LightDir.w * 11.0);
    float3 coreColor = lerp(float3(1, 1, 1), input.color.rgb, smoothstep(0.0, 0.34, r) * 0.55);
    float4 result = float4(input.color.rgb, glow * 0.75 * twinkle);
    result = Over(float4(kRimColor, rim * 0.45), result);
    result = Over(float4(coreColor, core), result);
    return result;
}

/** @brief 地面に寝かせる形（淡い面・波紋・足元の矢羽根と目盛り） */
float4 GroundShape(PS_INPUT input, float mode) {
    float2 p = (input.uv - 0.5) * 2.0;
    float r = length(p);
    float aa = fwidth(r);
    float angle = atan2(p.y, p.x);
    float3 color = input.color.rgb;
    float3 bright = lerp(color, float3(1, 1, 1), 0.35);
    float4 result = float4(0, 0, 0, 0);

    if (mode < 7.5) {
        // 止まりそうな範囲の内側。淡い面と細い縁
        result = float4(color, (1.0 - smoothstep(0.90, 0.96, r)) * 0.13);
        result = Over(float4(color, Band(r, 0.95, 0.018, aa) * 0.55), result);
    } else if (mode < 8.5) {
        // 広がる波紋（shape.y が進み具合 0〜1）
        float progress = saturate(input.shape.y);
        float eased = 1.0 - pow(1.0 - progress, 3.0);
        float radius = lerp(0.12, 0.92, eased);
        float thickness = lerp(0.10, 0.022, progress);
        float fade = pow(1.0 - progress, 1.4);
        result = float4(bright, Band(r, radius * 0.62, thickness * 0.7, aa) * fade * 0.4);
        result = Over(RimmedBand(r, radius, thickness, aa, bright, fade), result);
    } else if (mode < 9.5) {
        // 足元の矢羽根。前へ流れる（shape.y が位相）
        float forward = p.y;
        float lateral = abs(p.x);
        float t = saturate((forward - 0.22) / 0.76);
        float halfWidth = lerp(0.15, 0.06, t);
        float wave = frac((forward + lateral * 1.15) * 3.4 - input.shape.y);
        float chevron = smoothstep(0.0, 0.10, wave) * (1.0 - smoothstep(0.40, 0.52, wave));
        float chevronRim = smoothstep(0.0, 0.05, frac(wave + 0.08)) *
                           (1.0 - smoothstep(0.56, 0.66, frac(wave + 0.08)));
        float side = 1.0 - smoothstep(halfWidth - aa, halfWidth + aa, lateral);
        float sideRim = 1.0 - smoothstep(halfWidth * 1.3 - aa, halfWidth * 1.3 + aa, lateral);
        float fade = smoothstep(0.22, 0.34, forward + lateral * 1.15) * pow(1.0 - t, 1.2) *
                     step(0.0, forward);
        result = float4(kRimColor, chevronRim * sideRim * fade * 0.45);
        result = Over(float4(bright, chevron * side * fade), result);
        // ボールを囲む細い輪
        result = Over(RimmedBand(r, 0.165, 0.010, aa, bright, 0.85), result);
    } else {
        // 足元の方位目盛り（地面に固定。向きを回すと矢羽根との差で回転が分かる）
        float index = floor(angle / kTwoPi * 24.0 + 0.5);
        float s = abs(angle - index * kTwoPi / 24.0) * r;
        float major = step(abs(fmod(abs(index), 6.0)), 0.5);
        float inner = lerp(0.222, 0.205, major);
        float outer = lerp(0.248, 0.272, major);
        float radial = smoothstep(inner - aa, inner + aa, r) * (1.0 - smoothstep(outer - aa, outer + aa, r));
        float tick = (1.0 - smoothstep(0.0055 - aa, 0.0055 + aa, s)) * radial;
        float tickRim = (1.0 - smoothstep(0.012 - aa, 0.012 + aa, s)) *
                        smoothstep(inner - 0.012, inner, r) * (1.0 - smoothstep(outer, outer + 0.012, r));
        result = float4(kRimColor, tickRim * 0.4);
        result = Over(float4(color, tick * lerp(0.6, 1.0, major)), result);
    }
    return result;
}

/** @brief 静止点から立ちのぼる光の柱（shape.y が位相） */
float4 Beam(PS_INPUT input) {
    float a = abs(input.uv.x * 2.0 - 1.0);
    float v = input.uv.y;
    float across = pow(saturate(1.0 - a * a), 2.0);
    float rise = 0.62 + 0.38 * sin((v * 4.0 - input.shape.y) * kTwoPi);
    float alpha = across * pow(saturate(1.0 - v), 1.7) * rise;
    float3 color = lerp(input.color.rgb, float3(1, 1, 1), pow(saturate(1.0 - a), 3.0) * 0.6);
    return float4(color, alpha * 0.85);
}

SceneOutput main(PS_INPUT input) {
    float mode = input.shape.x;
    float4 result;
    if (mode < 1.5) {
        result = RibbonFlight(input);
    } else if (mode < 2.5) {
        result = RibbonRoll(input);
    } else if (mode < 3.5) {
        result = Orb(input);
    } else if (mode < 10.5) {
        result = GroundShape(input, mode);
    } else {
        result = Beam(input);
    }
    result.a *= input.color.a;
    clip(result.a - 0.004);
    return MakeSceneOutput(result, kCameraOnlyVelocity);
}
