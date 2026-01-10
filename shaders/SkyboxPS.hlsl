/**
 * @file SkyboxPS.hlsl
 * @brief スカイボックス用ピクセルシェーダー
 */

TextureCube skyboxTexture : register(t0);
SamplerState skyboxSampler : register(s0);

cbuffer SkyboxConstants : register(b0) {
    matrix View;
    matrix Projection;
    float4 TintColor;
    float Brightness;
    float Saturation;
    float2 Padding;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // キューブマップからサンプリング
    float4 color = skyboxTexture.Sample(skyboxSampler, input.texCoord);
    
    // 彩度調整（床の文字を見やすくするため）
    float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = lerp(float3(gray, gray, gray), color.rgb, Saturation);
    
    // 明度調整
    color.rgb *= Brightness;
    
    // ティント色適用
    color.rgb *= TintColor.rgb;
    
    return color;
}
