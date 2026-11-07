/**
 * @file TerrainDepthPS.hlsl
 * @brief 地形の深度だけを先に書くパス（色は書かない）
 * @details カップの開口部のくり抜き（discard）はここだけで行う。
 *          discard があると GPU は奥に隠れる画素でも早期に捨てられず、重い地形シェーダーを
 *          最後まで走らせてしまう。先に深度を確定させ、本描画（TerrainPS）は深度が一致する
 *          画素だけを塗ることで、地形の重ね塗りと隠れた画素の計算をなくす。
 *          頂点シェーダーは本描画と同じ TerrainVS を使い、深度が完全に一致するようにする。
 */

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BINORMAL;
    float4 ShadowPos : TEXCOORD4;
};

#include "GolfCupClip.hlsli"

void main(PS_INPUT input) {
    ClipGolfCupOpening(input.WorldPos);
}
