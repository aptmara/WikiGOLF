/**
 * @file FullscreenVS.hlsl
 * @brief 全画面クアッド用の頂点シェーダー（TransitionPSなどのフルスクリーン効果向け）
 */

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    // builtin/quad は -0.5〜0.5 の範囲なので、2倍してクリップ空間全体を覆う
    output.position = float4(input.position.xy * 2.0f, 0.0f, 1.0f);
    output.texCoord = input.texCoord;

    return output;
}
