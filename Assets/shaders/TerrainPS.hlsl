struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
};

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags; // x: hasTexture, y: hasNormalMap (unused here)
    float4 LightDir;
    float4 CameraPos;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 main(PS_INPUT input) : SV_TARGET {
    // 1. ベースカラー (頂点カラー * テクスチャ)
    float4 baseColor = input.Color;
    
    // テクスチャサンプリング
    // ※テクスチャがバインドされていない場合の挙動は不定だが、
    //   白テクスチャ扱いになることを期待するか、アプリケーション側で保証する。
    float4 texColor = g_Texture.Sample(g_Sampler, input.Tex);
    
    baseColor *= texColor;

    // 2. グリッドライン描画 (ワールド座標ベース)
    // 遠景でのモアレを防ぐため、カメラ距離に応じてフェードアウトさせる
    float dist = distance(CameraPos.xyz, input.WorldPos);
    float gridFade = 1.0f - saturate((dist - 30.0f) / 50.0f);
    
    if (gridFade > 0.0f) {
        float3 pos = input.WorldPos;
        
        // 1m グリッド
        float2 grid1 = abs(frac(pos.xz) - 0.5f);
        // 微分を使ってアンチエイリアス風にするのが理想だが、簡易的にstepで
        // 線の太さを一定に見せるため、fwidthを使いたいが、まずは固定値で
        float line1 = step(0.48f, max(grid1.x, grid1.y));
        
        // 10m グリッド
        float2 grid10 = abs(frac(pos.xz * 0.1f) - 0.5f);
        float line10 = step(0.495f, max(grid10.x, grid10.y));
        
        float4 gridColor = float4(0.0f, 0.0f, 0.0f, 0.15f); // 薄い黒
        
        // グリッドを合成
        float lineStrength = max(line1 * 0.5f, line10 * 1.0f) * gridFade;
        baseColor.rgb = lerp(baseColor.rgb, gridColor.rgb, lineStrength * gridColor.a);
    }
    
    // 3. ライティング (Lambert + Ambient)
    float3 N = normalize(input.Normal);
    float3 L = normalize(-LightDir.xyz); 
    
    float diff = max(dot(N, L), 0.0f);
    float3 ambient = float3(0.4f, 0.4f, 0.5f); // 青みがかった環境光
    float3 lightColor = float3(1.0f, 0.95f, 0.9f); // 太陽光
    
    float3 lighting = ambient + lightColor * diff;
    
    float4 finalColor = float4(baseColor.rgb * lighting, baseColor.a);
    
    // 4. フォグ (Distance Fog)
    float fogStart = 20.0f;
    float fogEnd = 120.0f; // 少し遠くまで見えるように
    float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart));
    
    // フォグ色は空の色に合わせる (ここでは空色固定)
    float3 fogColor = float3(0.7f, 0.85f, 1.0f); 
    
    finalColor.rgb = lerp(finalColor.rgb, fogColor, fogFactor);
    
    return finalColor;
}
