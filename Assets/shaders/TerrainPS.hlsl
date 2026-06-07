struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
    float3 Tangent : TANGENT;
    float3 Bitangent : BINORMAL;
};

cbuffer ConstantBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 MaterialColor;
    float4 MaterialFlags; // x: hasTexture, y: hasNormalMap, z: uvScale
    float4 LightDir;
    float4 CameraPos;
};

// t0: Albedo Array (8 layers), t1: Normal Array (8 layers)
Texture2DArray g_AlbedoArray : register(t0);
Texture2DArray g_NormalArray : register(t1);
SamplerState g_Sampler : register(s0);

float4 main(PS_INPUT input) : SV_TARGET {
    // UVスケール適用
    float uvScale = max(MaterialFlags.z, 1.0f);
    float3 uvw = float3(input.Tex * uvScale, 0.0f);

    // 2. ベースカラー。
    // 素材IDを補間してTexture2DArrayのlayerに使うと、境界に水/OB等の偽素材が出る。
    // 地形本体は連続した頂点色を正とし、質感だけ共通レイヤーから薄く加える。
    float4 baseColor = float4(input.Color.rgb, 1.0f);
    
    // テクスチャサンプリング
    float4 texColor = g_AlbedoArray.Sample(g_Sampler, uvw);
    baseColor.rgb *= lerp(float3(1.0f, 1.0f, 1.0f), texColor.rgb, 0.35f);

    // 3. 法線計算
    float3 N = normalize(input.Normal);
    if (MaterialFlags.y > 0.5f) {
        float3 T = normalize(input.Tangent);
        float3 B = normalize(input.Bitangent);
        float3x3 TBN = float3x3(T, B, N);
        
        float3 mapN = g_NormalArray.Sample(g_Sampler, uvw).xyz;
        mapN = mapN * 2.0f - 1.0f;
        
        // 法線の強さを強調 (XYを強調することで凹凸を際立たせる)
        mapN.xy *= 1.5f; 
        N = normalize(mul(mapN, TBN));
    }

    // 4. ライティング
    float3 L = normalize(-LightDir.xyz); 
    float diff = max(dot(N, L), 0.0f);
    float3 ambient = float3(0.35f, 0.35f, 0.4f);
    float3 lightColor = float3(1.0f, 0.98f, 0.95f);
    
    float3 lighting = ambient + lightColor * diff;
    
    // 5. グリッドライン (ワールド座標ベース)
    float dist = distance(CameraPos.xyz, input.WorldPos);
    float gridFade = 1.0f - saturate((dist - 40.0f) / 60.0f);
    if (gridFade > 0.0f) {
        float2 grid1 = abs(frac(input.WorldPos.xz) - 0.5f);
        float line1 = step(0.485f, max(grid1.x, grid1.y));
        float2 grid10 = abs(frac(input.WorldPos.xz * 0.1f) - 0.5f);
        float line10 = step(0.496f, max(grid10.x, grid10.y));
        
        float lineStrength = max(line1 * 0.4f, line10 * 0.8f) * gridFade;
        baseColor.rgb = lerp(baseColor.rgb, float3(0,0,0), lineStrength * 0.2f);
    }

    // 最終カラー。アルファは必ず 1.0 にする
    float4 finalColor = float4(baseColor.rgb * lighting, 1.0f);
    
    // 6. フォグ
    float fogStart = 60.0f;
    float fogEnd = 400.0f;
    float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart));
    float3 fogColor = float3(0.75f, 0.85f, 0.95f); 
    finalColor.rgb = lerp(finalColor.rgb, fogColor, fogFactor);
    
    return finalColor;
}
