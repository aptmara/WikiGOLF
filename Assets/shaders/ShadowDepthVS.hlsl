/**
 * @file ShadowDepthVS.hlsl
 * @brief 平行光シャドウマップ用深度頂点シェーダー
 */

cbuffer ShadowConstants : register(b0) {
    matrix World;
    matrix LightViewProjection;
};

struct VS_INPUT {
    float3 position : POSITION;
};

float4 main(VS_INPUT input) : SV_POSITION {
    float4 worldPosition = mul(float4(input.position, 1.0f), World);
    return mul(worldPosition, LightViewProjection);
}
