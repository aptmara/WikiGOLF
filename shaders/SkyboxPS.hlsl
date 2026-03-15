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
    float Time;
    float Padding;
    float3 SunDirection;
    float Padding2;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // キューブマップをゆっくり回転（流れる雲）
    float angle = Time * 0.005f;
    float s, c;
    sincos(angle, s, c);
    
    float3 rotatedTexCoord = input.texCoord;
    rotatedTexCoord.x = input.texCoord.x * c - input.texCoord.z * s;
    rotatedTexCoord.z = input.texCoord.x * s + input.texCoord.z * c;

    // キューブマップからサンプリング
    float4 color = skyboxTexture.Sample(skyboxSampler, rotatedTexCoord);
    
    float3 viewDir = normalize(input.texCoord);
    float3 sunDir = normalize(SunDirection);
    float dotSun = dot(viewDir, sunDir);
    
    // 太陽のグロー
    float sunGlow = saturate((dotSun - 0.95f) * 20.0f);
    sunGlow = pow(sunGlow, 2.0f);
    
    // 雲の輝度抽出（白に近い部分）
    float cloudness = saturate((color.r + color.g + color.b - 1.8f) * 1.5f);
    
    // 太陽と雲のハイライト合成
    color.rgb += float3(1.0f, 0.95f, 0.9f) * sunGlow * 1.5f;
    color.rgb += float3(1.0f, 1.0f, 1.0f) * cloudness * sunGlow * 1.0f;
    
    // 大気散乱（地平線の霞）
    float horizonMix = pow(1.0f - abs(viewDir.y), 4.0f);
    color.rgb = lerp(color.rgb, float3(0.7f, 0.85f, 1.0f), horizonMix * 0.25f);
    
    // 彩度調整
    float gray = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
    color.rgb = lerp(float3(gray, gray, gray), color.rgb, Saturation);
    
    // 明度調整
    color.rgb *= Brightness;
    
    // ティント色適用
    color.rgb *= TintColor.rgb;
    
    return float4(saturate(color.rgb), color.a);
}
