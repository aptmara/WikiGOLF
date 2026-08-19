/**
 * @file GrassPS.hlsl
 * @brief 芝ブレード専用のピクセルシェーダー。
 *        根本のAO、頂点カラーの根本→穂先グラデーション、逆光時の疑似透過(SSS風)、
 *        地形と統一したフォグを適用する。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
    float4 LightDir;
    float4 CameraPos;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float tipWeight = saturate(1.0f - input.texCoord.y); // 根本=0, 穂先=1

    float3 N = normalize(input.normal);
    float3 L = normalize(-LightDir.xyz);
    float3 V = normalize(CameraPos.xyz - input.worldPos);

    float diffuse = max(dot(N, L), 0.0f);
    // ラップライト。葉の裏側が真っ黒に落ちすぎないようにする。
    float wrap = saturate(dot(N, L) * 0.5f + 0.5f);

    // 太陽を透かして穂先が明るくなる疑似的な半透過（Subsurface風）。
    float backlight = pow(saturate(dot(-L, V)), 3.0f) * tipWeight * 0.55f;

    // 根本は地面に接しているため影になりやすい（疑似AO）。深く落として
    // 個々の葉が土から浮いて見えず、地面と一体化した芝生の質感にする。
    float ao = lerp(0.42f, 1.0f, tipWeight * tipWeight);

    float3 ambient = float3(0.30f, 0.34f, 0.27f) * ao;
    float3 lightColor = float3(1.0f, 0.97f, 0.88f);
    float3 lit = ambient + lightColor * lerp(wrap, diffuse, 0.5f) * ao;

    float3 baseColor = input.color.rgb;
    float3 finalColor = baseColor * lit + lightColor * backlight * baseColor;

    // 地形と統一したフォグ
    float dist = distance(CameraPos.xyz, input.worldPos);
    float fogStart = 60.0f;
    float fogEnd = 400.0f;
    float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart));
    float3 fogColor = float3(0.75f, 0.85f, 0.95f);
    finalColor = lerp(finalColor, fogColor, fogFactor);

    return float4(finalColor, 1.0f);
}
