/**
 * @file TerrainDepthFastPS.hlsl
 * @brief 地形の深度だけを先に書くパスのうち、カップに掛からないタイル用（何もしない）
 * @details discard もループもないので、隠れた画素は早期の深度テストでそのまま捨てられる。
 *          カップに掛かるタイルは TerrainDepthPS でくり抜く。
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

void main(PS_INPUT input) {
}
