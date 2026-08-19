/**
 * @file GrassVS.hlsl
 * @brief 密生芝のインスタンシング描画。常時のそよ風ゆれ・ボール接触変形・
 *        株ごとの明度/位相差（同じテンプレートの複製に見えないための個体差）を行う。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
    float4 LightDir; // w: 経過時間（秒）
    float4 CameraPos;
};

struct InstanceData {
    matrix World;
    float4 Color;
    // xy: 最後の接触位置、z: 曲がる方向の角度、w: 曲がり量
    float4 Flags;
};

StructuredBuffer<InstanceData> g_instances : register(t15);

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 tangent : TANGENT;
    float3 bitangent : BINORMAL;
    uint instanceID : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
};

float Hash21(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    InstanceData inst = g_instances[input.instanceID];

    float4 worldPos = mul(float4(input.position, 1.0f), inst.World);

    // 株（ブレード）ごとの疑似乱数シード。ワールド座標そのものを種にするので
    // 1パッチ内のブレード同士でも明度・風位相がばらつき、量産テンプレートの
    // 貼り付けに見えるのを防ぐ。
    float bladeRandom = Hash21(worldPos.xz);
    // 個体差が強すぎると「野生の草むら」に見えるため、同じ品種の芝が
    // 均一に生えている印象になる範囲に絞る。
    float individualTint = 0.88f + bladeRandom * 0.24f;
    float windPhase = Hash21(worldPos.xz + 91.7f) * 6.2831853f;

    float materialClass = saturate(inst.Color.a);
    float tipWeight = saturate(1.0f - input.texCoord.y); // 根本=0, 穂先=1

    // --- ボール接触による倒れ込み ---
    float2 fromContact = worldPos.xz - inst.Flags.xy;
    float responseRadius = lerp(0.55f, 1.05f, materialClass);
    float contactFalloff = saturate(1.0f - length(fromContact) / responseRadius);
    contactFalloff = contactFalloff * contactFalloff * (3.0f - 2.0f * contactFalloff);
    float bend = saturate(inst.Flags.w) * contactFalloff * tipWeight;
    float2 bendDirection = float2(cos(inst.Flags.z), sin(inst.Flags.z));
    float displacement = lerp(0.045f, 0.22f, materialClass) * bend;
    worldPos.xz += bendDirection * displacement;
    worldPos.y -= lerp(0.018f, 0.12f, materialClass) * bend * bend;

    // --- 常時ゆれる環境風。株ごとに位相をずらした2波の合成で、
    //     一斉に同じ動きをする不自然さを避け生きた芝に見せる ---
    float time = LightDir.w;
    float2 windDir = normalize(float2(0.62f, 0.78f));
    float travel = dot(worldPos.xz, windDir);
    float gust = sin(travel * 0.55f + time * 1.7f + windPhase) * 0.6f +
                sin(travel * 1.9f - time * 2.6f + windPhase * 1.3f) * 0.4f;
    float windSway = gust * lerp(0.045f, 0.09f, materialClass) * tipWeight;
    worldPos.xz += windDir * windSway;
    worldPos.y -= abs(windSway) * 0.12f;

    output.position = mul(mul(worldPos, View), Projection);

    float3x3 world3x3 = (float3x3)inst.World;
    float3 normal = normalize(mul(input.normal, world3x3));
    float2 leanTotal = bendDirection * bend * 0.42f + windDir * windSway * 3.6f;
    normal = normalize(normal + float3(-leanTotal.x,
                                       (bend + abs(windSway)) * 0.18f,
                                       -leanTotal.y));
    output.normal = normal;
    output.texCoord = input.texCoord;
    output.color = float4(input.color.rgb * inst.Color.rgb * individualTint, 1.0f);
    output.worldPos = worldPos.xyz;
    return output;
}
