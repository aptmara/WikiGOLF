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

float TurfFibers(float2 worldXZ, float2 bladeDirection, float density) {
    float2 direction = normalize(bladeDirection);
    float2 across = float2(-direction.y, direction.x);
    float2 orientedPosition =
        float2(dot(worldXZ, direction), dot(worldXZ, across));
    float2 gridPosition = orientedPosition * float2(density * 0.36f, density);
    float2 cell = floor(gridPosition);
    float2 local = frac(gridPosition) - 0.5f;
    float seed = SandHash(cell);
    float2 offset =
        float2(SandHash(cell + 17.3f), SandHash(cell + 41.7f)) - 0.5f;
    offset *= 0.42f;
    float2 bladeDelta = local - offset;
    float bladeDistance = length(float2(bladeDelta.x * 0.32f, bladeDelta.y));
    float fiber = 1.0f - smoothstep(0.045f, 0.19f, bladeDistance);
    return fiber * (0.55f + seed * 0.45f);
}

static const float3 kTerrainPalette[8] = {
    float3(0.25f, 0.43f, 0.16f),
    float3(0.18f, 0.32f, 0.11f),
    float3(0.90f, 0.85f, 0.70f),
    float3(0.30f, 0.52f, 0.19f),
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
    float shortTurfWeight = saturate(fairwayWeight + greenWeight);

    // 頂点色の補間に合わせ、最も近い2種類の地表テクスチャを連続的に混ぜる。
    float4 baseColor = float4(input.Color.rgb, 1.0f);
    
    // テクスチャサンプリング
    if (MaterialFlags.x > 0.5f) {
        float4 texColorA = g_AlbedoArray.Sample(g_Sampler, uvwA);
        float4 texColorB = g_AlbedoArray.Sample(g_Sampler, uvwB);
        float4 texColor = lerp(texColorA, texColorB, materialBlend);
        float textureStrength = lerp(0.45f, 0.08f, shortTurfWeight);
        baseColor.rgb *=
            lerp(float3(1.0f, 1.0f, 1.0f), texColor.rgb, textureStrength);
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

    // 中遠距離でも短芝が連続面に見えるよう、刈り方向へ伸びる
    // 複数スケールの微細繊維と、葉が倒れる向きの反転を加える。
    float2 turfWorld = input.WorldPos.xz;
    float fairwayFiber = TurfFibers(turfWorld, float2(0.0f, 1.0f), 18.0f);
    float fairwayFiberFine =
        TurfFibers(turfWorld + 9.4f, float2(0.0f, 1.0f), 38.0f);
    float roughFiber = TurfFibers(turfWorld, float2(0.64f, 0.77f), 11.0f);
    float roughFiberFine =
        TurfFibers(turfWorld + 13.7f, float2(-0.38f, 0.92f), 24.0f);
    float greenFiber = TurfFibers(turfWorld, float2(1.0f, 0.0f), 30.0f);
    float greenFiberFine =
        TurfFibers(turfWorld + 21.6f, float2(1.0f, 0.0f), 56.0f);

    float fairwayStripeWave = sin(turfWorld.x * (6.28318f / 3.2f));
    float greenStripeWave = sin(turfWorld.y * (6.28318f / 1.8f));
    float fairwayMowDirection = fairwayStripeWave >= 0.0f ? 1.0f : -1.0f;
    float greenMowDirection = greenStripeWave >= 0.0f ? 1.0f : -1.0f;
    float broadVariation =
        (SandHash(floor(turfWorld * 0.72f)) - 0.5f) * 0.035f;

    float turfShade =
        fairwayWeight * (fairwayFiber * 0.045f +
                         fairwayFiberFine * 0.032f +
                         fairwayMowDirection * 0.016f + broadVariation) +
        roughWeight * (roughFiber * 0.085f + roughFiberFine * 0.045f +
                       broadVariation * 1.4f) +
        greenWeight * (greenFiber * 0.028f + greenFiberFine * 0.020f +
                       greenMowDirection * 0.012f + broadVariation * 0.45f);
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
        mapN.xy *= lerp(0.85f, 0.28f, shortTurfWeight);
        N = normalize(mul(mapN, TBN));
    }

    // 刈り跡の明暗を単純な色帯ではなく、葉が寝る向きによる法線差として
    // 作る。視点と光源が変わると順目・逆目の見え方も変化する。
    float3 fairwayMowNormal =
        normalize(N + float3(0.0f, 0.0f, fairwayMowDirection * 0.085f));
    float3 greenMowNormal =
        normalize(N + float3(greenMowDirection * 0.045f, 0.0f, 0.0f));
    N = normalize(lerp(N, fairwayMowNormal, fairwayWeight * 0.82f));
    N = normalize(lerp(N, greenMowNormal, greenWeight * 0.72f));

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
    float3 V = normalize(CameraPos.xyz - input.WorldPos);
    float3 H = normalize(L + V);
    
    float3 lighting = ambient + lightColor * diff;
    
    float dist = distance(CameraPos.xyz, input.WorldPos);

    // 最終カラー。アルファは必ず 1.0 にする
    float4 finalColor = float4(baseColor.rgb * lighting, 1.0f);

    // 細い刈り込み葉が同じ方向へ揃うことで生じる弱い異方性光沢。
    float2 halfXZ = normalize(H.xz + float2(0.0001f, 0.0001f));
    float fairwayAlong = abs(dot(halfXZ, float2(0.0f, 1.0f)));
    float greenAlong = abs(dot(halfXZ, float2(1.0f, 0.0f)));
    float normalHighlight = pow(saturate(dot(N, H)), 24.0f);
    float turfSheen =
        normalHighlight *
        (fairwayWeight * (0.025f + fairwayAlong * 0.045f) +
         greenWeight * (0.035f + greenAlong * 0.060f) +
         roughWeight * 0.012f);
    finalColor.rgb += lightColor * turfSheen;

    // ごく一部の砂粒だけが強く反射し、均一な黄色い面に見えるのを防ぐ。
    float viewFacing = saturate(dot(N, V));
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
