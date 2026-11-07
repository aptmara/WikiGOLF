/**
 * @file GrassWindCS.hlsl
 * @brief カメラ周り 128m 四方の風を 1m 間隔で求める
 * @details 遠くまで伝わる風の帯、突風、風下へ流れるつむじ風と、その後ろの二次波を重ねる。
 *          葉ごとの細かな揺れは GrassFieldVS.hlsl で足す。
 *          出力 xy: 葉先の横ずれ[m]（しなやかさ 1 の葉）、z: つむじ風の見た目の強さ、w: 突風の包絡
 */

#include "GrassFieldCommon.hlsli"

RWTexture2D<float4> g_Wind : register(u0);

float Hash21(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float2 position = GrassWindOrigin.xy + (float2(id.xy) + 0.5f) * kWindTexelSize;
    float time = GrassLight.w;
    float2 windDir = GrassWindParams.xy;
    float2 crossWindDir = float2(-windDir.y, windDir.x);
    float windStrength = GrassWindParams.z;
    float windActivity = smoothstep(0.015f, 0.12f, windStrength);
    float windAmplitude = lerp(0.004f, 0.115f, windStrength);

    float travel = dot(position, windDir);
    float broadWave = sin(travel * 0.16f - time * (0.72f + windStrength * 1.05f));
    float gustWave = sin(travel * 0.43f - time * (1.18f + windStrength * 1.85f));
    float gustEnvelope = saturate(0.52f + broadWave * 0.30f + gustWave * 0.18f);
    gustEnvelope = gustEnvelope * gustEnvelope * (3.0f - 2.0f * gustEnvelope);

    float downwindLean = windAmplitude * windActivity * (0.18f + gustEnvelope * 0.48f);
    float2 offset = windDir * downwindLean;

    // 疎な局所風を風下へ流す。横帯ごとに間隔を空け、一部を間引く
    float alongPosition = travel;
    float crossPosition = dot(position, crossWindDir);
    const float vortexBandSpacing = 20.0f;
    const float vortexPacketSpacing = 62.0f;
    float vortexBand = floor(crossPosition / vortexBandSpacing);
    float bandRandom = Hash21(float2(vortexBand, 37.19f));
    float vortexCrossCenter = (vortexBand + 0.5f) * vortexBandSpacing + (bandRandom - 0.5f) * 3.0f;
    float vortexTravel = time * lerp(2.8f, 6.4f, windStrength);
    float packetCoordinate = alongPosition - vortexTravel + bandRandom * vortexPacketSpacing;
    float vortexPacket = floor(packetCoordinate / vortexPacketSpacing);
    float localAlong = (frac(packetCoordinate / vortexPacketSpacing) - 0.5f) * vortexPacketSpacing;
    float localCross = crossPosition - vortexCrossCenter;
    float packetRandom = Hash21(float2(vortexBand + 83.7f, vortexPacket - 19.4f));
    float packetExists = smoothstep(0.12f, 0.24f, packetRandom);

    float vortexAlongRadius = lerp(9.0f, 13.0f, windStrength);
    float vortexCrossRadius = lerp(4.8f, 7.0f, windStrength);
    float vortexDistance = length(float2(localAlong / vortexAlongRadius,
                                         localCross / vortexCrossRadius));
    float vortexCoreMask = saturate(1.0f - vortexDistance);
    vortexCoreMask = vortexCoreMask * vortexCoreMask * (3.0f - 2.0f * vortexCoreMask);
    float vortexCrest = 1.0f - smoothstep(0.08f, 0.30f, abs(vortexDistance - 0.58f));
    float vortexMask = saturate(vortexCoreMask + vortexCrest * 0.48f) * packetExists * windActivity;

    float wakeAlong = saturate(1.0f - abs(localAlong) / (vortexAlongRadius * 1.35f));
    float wakeAcross = saturate(1.0f - abs(localCross) / (vortexCrossRadius * 1.20f));
    float wakeRipple = 0.5f + 0.5f * sin(localAlong * 1.05f -
                                         time * (2.4f + windStrength * 2.8f) +
                                         bandRandom * 6.2831853f);
    wakeRipple = wakeRipple * wakeRipple * wakeAlong * wakeAcross * packetExists * windActivity;

    float2 fromVortex = windDir * localAlong + crossWindDir * localCross;
    float2 radialDirection = fromVortex / max(length(fromVortex), 0.001f);
    float spinDirection = bandRandom < 0.5f ? -1.0f : 1.0f;
    float2 vortexTangent = float2(-radialDirection.y, radialDirection.x) * spinDirection;
    float vortexRing = saturate(vortexDistance * 2.4f);
    float vortexAmplitude = lerp(0.065f, 0.280f, windStrength);
    offset += (vortexTangent * lerp(0.48f, 1.18f, vortexRing) + windDir * 0.68f) *
              vortexAmplitude * vortexMask;
    offset += windDir * vortexAmplitude * 0.34f * wakeRipple;
    float vortexVisual = saturate(vortexMask * 0.88f + wakeRipple * 0.52f);

    g_Wind[id.xy] = float4(offset, vortexVisual, gustEnvelope);
}
