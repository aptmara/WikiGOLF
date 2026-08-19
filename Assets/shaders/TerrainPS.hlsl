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

float SandHash(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float TurfFibers(float2 worldXZ, float density) {
    float2 gridPosition = worldXZ * density;
    float2 cell = floor(gridPosition);
    float2 local = frac(gridPosition) - 0.5f;
    float seed = SandHash(cell);
    float2 offset =
        float2(SandHash(cell + 17.3f), SandHash(cell + 41.7f)) - 0.5f;
    offset *= 0.42f;
    float bladeDistance = length(local - offset);
    float fiber = 1.0f - smoothstep(0.055f, 0.23f, bladeDistance);
    return fiber * (0.55f + seed * 0.45f);
}

static const float3 kTerrainPalette[8] = {
    float3(0.35f, 0.55f, 0.25f),
    float3(0.25f, 0.45f, 0.20f),
    float3(0.90f, 0.85f, 0.70f),
    float3(0.40f, 0.75f, 0.30f),
    float3(0.70f, 0.88f, 0.98f),
    float3(0.20f, 0.45f, 0.85f),
    float3(0.95f, 0.35f, 0.12f),
    float3(0.50f, 0.48f, 0.52f)
};

void FindMaterialBlend(float3 color, out float layerA, out float layerB,
                       out float blend) {
    float nearestDistance = 1e9f;
    float secondDistance = 1e9f;
    int nearestLayer = 0;
    int secondLayer = 0;

    [unroll]
    for (int layer = 0; layer < 8; ++layer) {
        float3 delta = color - kTerrainPalette[layer];
        float distanceSq = dot(delta, delta);
        if (distanceSq < nearestDistance) {
            secondDistance = nearestDistance;
            secondLayer = nearestLayer;
            nearestDistance = distanceSq;
            nearestLayer = layer;
        } else if (distanceSq < secondDistance) {
            secondDistance = distanceSq;
            secondLayer = layer;
        }
    }

    float nearestWeight = rcp(nearestDistance + 0.0001f);
    float secondWeight = rcp(secondDistance + 0.0001f);
    layerA = (float)nearestLayer;
    layerB = (float)secondLayer;
    blend = secondWeight / (nearestWeight + secondWeight);
}

float4 main(PS_INPUT input) : SV_TARGET {
    // UVスケール適用
    float uvScale = max(MaterialFlags.z, 1.0f);
    float materialLayerA;
    float materialLayerB;
    float materialBlend;
    FindMaterialBlend(input.Color.rgb, materialLayerA, materialLayerB,
                      materialBlend);
    float3 uvwA = float3(input.Tex * uvScale, materialLayerA);
    float3 uvwB = float3(input.Tex * uvScale, materialLayerB);
    float bunkerLayerA = 1.0f - saturate(abs(materialLayerA - 2.0f));
    float bunkerLayerB = 1.0f - saturate(abs(materialLayerB - 2.0f));
    float bunkerWeight = lerp(bunkerLayerA, bunkerLayerB, materialBlend);
    float fairwayWeight = lerp(1.0f - saturate(abs(materialLayerA)),
                               1.0f - saturate(abs(materialLayerB)),
                               materialBlend);
    float roughWeight = lerp(1.0f - saturate(abs(materialLayerA - 1.0f)),
                             1.0f - saturate(abs(materialLayerB - 1.0f)),
                             materialBlend);
    float greenWeight = lerp(1.0f - saturate(abs(materialLayerA - 3.0f)),
                             1.0f - saturate(abs(materialLayerB - 3.0f)),
                             materialBlend);

    // 頂点色の補間に合わせ、最も近い2種類の地表テクスチャを連続的に混ぜる。
    float4 baseColor = float4(input.Color.rgb, 1.0f);
    
    // テクスチャサンプリング
    if (MaterialFlags.x > 0.5f) {
        float4 texColorA = g_AlbedoArray.Sample(g_Sampler, uvwA);
        float4 texColorB = g_AlbedoArray.Sample(g_Sampler, uvwB);
        float4 texColor = lerp(texColorA, texColorB, materialBlend);
        baseColor.rgb *= lerp(float3(1.0f, 1.0f, 1.0f), texColor.rgb, 0.45f);
    }

    // バンカー専用の微細な砂粒と、風でできた複数スケールの波紋。
    float2 sandWorld = input.WorldPos.xz;
    float sandGrain = SandHash(floor(sandWorld * 145.0f));
    float sandGrainFine = SandHash(floor(sandWorld * 310.0f + 17.0f));
    float windRippleA = sin(dot(sandWorld, float2(0.94f, 0.34f)) * 18.0f +
                            sin(sandWorld.y * 2.7f) * 0.65f);
    float windRippleB = sin(dot(sandWorld, float2(-0.28f, 0.96f)) * 7.5f +
                            windRippleA * 0.55f);
    float sandShade = 0.91f + sandGrain * 0.13f + sandGrainFine * 0.045f +
                      windRippleA * 0.035f + windRippleB * 0.022f;
    float3 warmSand = baseColor.rgb * sandShade * float3(1.06f, 1.00f, 0.88f);
    baseColor.rgb = lerp(baseColor.rgb, warmSand, bunkerWeight);

    // 遠景や3D葉の隙間も連続した芝面に見せる微細な繊維と、
    // ゴルフ場らしい「刈り跡ストライプ」。野生の草地ではなく手入れされた
    // 芝生に見せる最大の手掛かりなので、帯がはっきり読める強さで入れる。
    float2 turfWorld = input.WorldPos.xz;
    float fairwayFiber = TurfFibers(turfWorld, 10.0f);
    float roughFiber = TurfFibers(turfWorld, 7.0f);
    // ラフは3D葉が疎らな場所でも地面自体が「土」ではなく「芝」に見えるよう、
    // 粗密2スケールの繊維を重ねて密度感を底上げする。
    float roughFiberFine = TurfFibers(turfWorld, 21.0f);
    float greenFiber = TurfFibers(turfWorld, 16.0f);

    float fairwayStripeWave = sin(turfWorld.x * (6.28318f / 2.6f));
    float fairwayStripe =
        (smoothstep(-0.12f, 0.12f, fairwayStripeWave) - 0.5f) * 0.11f;
    float greenStripeWave = sin(turfWorld.y * (6.28318f / 1.5f));
    float greenStripe =
        (smoothstep(-0.14f, 0.14f, greenStripeWave) - 0.5f) * 0.07f;
    // ラフにもうっすらとした刈り跡の目を入れ、伸びた草地ではなく
    // 手入れされたラフであることをほのめかす（フェアウェイほど強くしない）。
    float roughGrain =
        sin(turfWorld.x * (6.28318f / 0.85f) + turfWorld.y * 0.55f) * 0.025f;

    float turfShade = fairwayWeight * (fairwayFiber * 0.05f + fairwayStripe) +
                      roughWeight *
                          (roughFiber * 0.16f + roughFiberFine * 0.11f +
                           roughGrain) +
                      greenWeight * (greenFiber * 0.03f + greenStripe);
    baseColor.rgb *= 1.0f + turfShade;

    // 3. 法線計算
    float3 N = normalize(input.Normal);
    if (MaterialFlags.y > 0.5f) {
        float3 T = normalize(input.Tangent);
        float3 B = normalize(input.Bitangent);
        float3x3 TBN = float3x3(T, B, N);
        
        float3 mapNA = g_NormalArray.Sample(g_Sampler, uvwA).xyz;
        float3 mapNB = g_NormalArray.Sample(g_Sampler, uvwB).xyz;
        float3 mapN = lerp(mapNA, mapNB, materialBlend);
        mapN = mapN * 2.0f - 1.0f;
        
        // 地形の起伏を覆わない程度に法線マップの細部を加える。
        mapN.xy *= 0.85f;
        N = normalize(mul(mapN, TBN));
    }

    float sandDx = SandHash(floor((sandWorld + float2(0.008f, 0.0f)) * 145.0f)) -
                   sandGrain;
    float sandDz = SandHash(floor((sandWorld + float2(0.0f, 0.008f)) * 145.0f)) -
                   sandGrain;
    N = normalize(N + float3(sandDx, 0.0f, sandDz) * bunkerWeight * 0.18f);

    // 4. ライティング
    float3 L = normalize(-LightDir.xyz); 
    float diff = max(dot(N, L), 0.0f);
    float3 ambient = float3(0.35f, 0.35f, 0.4f);
    float3 lightColor = float3(1.0f, 0.98f, 0.95f);
    
    float3 lighting = ambient + lightColor * diff;
    
    float dist = distance(CameraPos.xyz, input.WorldPos);

    // 最終カラー。アルファは必ず 1.0 にする
    float4 finalColor = float4(baseColor.rgb * lighting, 1.0f);

    // ごく一部の砂粒だけが強く反射し、均一な黄色い面に見えるのを防ぐ。
    float viewFacing = saturate(dot(N, normalize(CameraPos.xyz - input.WorldPos)));
    float grainSparkle = pow(saturate(sandGrainFine), 24.0f) *
                         pow(viewFacing, 5.0f) * diff;
    finalColor.rgb += bunkerWeight * grainSparkle *
                      float3(0.42f, 0.31f, 0.13f);
    
    // 6. フォグ
    float fogStart = 60.0f;
    float fogEnd = 400.0f;
    float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart));
    float3 fogColor = float3(0.75f, 0.85f, 0.95f); 
    finalColor.rgb = lerp(finalColor.rgb, fogColor, fogFactor);
    
    return finalColor;
}
