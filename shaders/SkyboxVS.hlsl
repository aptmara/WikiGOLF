/**
 * @file SkyboxVS.hlsl
 * @brief スカイボックス用頂点シェーダー
 */

cbuffer SkyboxConstants : register(b0) {
    matrix View;
    matrix Projection;
    float4 TintColor;
    float Brightness;
    float Saturation;
    float2 Padding;
};

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
    float3 texCoord : TEXCOORD;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    // カメラの回転のみを適用（移動は無視）
    // Viewマトリックスから移動成分を除去
    matrix viewNoTranslation = View;
    viewNoTranslation._41 = 0.0f;
    viewNoTranslation._42 = 0.0f;
    viewNoTranslation._43 = 0.0f;
    
    // 頂点を変換
    float4 pos = mul(float4(input.position, 1.0f), viewNoTranslation);
    output.position = mul(pos, Projection);
    
    // 深度を最大にして常に背景として描画
    output.position.z = output.position.w;
    
    // テクスチャ座標として元の頂点位置を使用
    output.texCoord = input.position;
    
    return output;
}
