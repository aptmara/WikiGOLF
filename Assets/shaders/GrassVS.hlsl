/**
 * @file GrassVS.hlsl
 * @brief 管理されたラフ芝のインスタンシング描画。
 *        ゲーム内風、ボール接触、葉ごとの小さな個体差を扱う。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    // xy: ゲーム内風向、z: 風速(m/s)
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
    float distanceFade : TEXCOORD2;
    float materialClass : TEXCOORD3;
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

    float bladeRandom = Hash21(worldPos.xz);
    float individualTint = 0.94f + bladeRandom * 0.12f;
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
    float flexibility = materialClass * materialClass;
    float displacement = lerp(0.002f, 0.15f, flexibility) * bend;
    worldPos.xz += bendDirection * displacement;
    worldPos.y -= lerp(0.001f, 0.075f, flexibility) * bend * bend;

    // ゲーム内の風向・風速へ連動する。無風時は葉先が完全に静止して見える
    // 不自然さだけを避ける、ごく弱い揺れに留める。
    float time = LightDir.w;
    float2 windVector = MaterialFlags_unused.xy;
    float windLength = length(windVector);
    float2 windDir = windLength > 0.0001f
        ? windVector / windLength
        : float2(0.62f, 0.78f);
    float windStrength = saturate(MaterialFlags_unused.z / 12.0f);
    float travel = dot(worldPos.xz, windDir);
    float gust = sin(travel * 0.42f + time * (0.65f + windStrength * 1.4f) +
                     windPhase) * 0.72f +
                 sin(travel * 1.35f - time * (1.1f + windStrength * 1.8f) +
                     windPhase * 1.3f) * 0.28f;
    float windAmplitude = lerp(0.006f, 0.065f, windStrength);
    float surfaceFlex = 0.08f + materialClass * 0.92f;
    float windSway = gust * windAmplitude * surfaceFlex * tipWeight;
    worldPos.xz += windDir * windSway;
    worldPos.y -= abs(windSway) * 0.12f;

    output.position = mul(mul(worldPos, View), Projection);

    // SRT行列の各基底からスケールを除いて法線を変換する。草丈と横幅で
    // 非等方スケールしても、照明法線が押し潰されないようにする。
    float3x3 world3x3 = (float3x3)inst.World;
    float3x3 rotationOnly = float3x3(normalize(world3x3[0]),
                                     normalize(world3x3[1]),
                                     normalize(world3x3[2]));
    float3 normal = normalize(mul(input.normal, rotationOnly));
    float2 leanTotal = bendDirection * bend * 0.42f + windDir * windSway * 3.6f;
    normal = normalize(normal + float3(-leanTotal.x,
                                       (bend + abs(windSway)) * 0.18f,
                                       -leanTotal.y));
    output.normal = normal;
    output.texCoord = input.texCoord;
    output.color = float4(input.color.rgb * inst.Color.rgb * individualTint, 1.0f);
    output.worldPos = worldPos.xyz;
    float cameraDistance = distance(CameraPos.xyz, worldPos.xyz);
    float fadeEnd = 14.0f + materialClass * 50.0f;
    float fadeWidth = 5.0f + materialClass * 7.0f;
    float distanceFade = saturate((fadeEnd - cameraDistance) / fadeWidth);

    // フェアウェイとグリーンは低角度でだけ立体葉を見せる。見下ろし時は
    // 地形側の密な短芝表現へ移行し、点状に見える小さな葉を残さない。
    float fairwayClass =
        1.0f - smoothstep(0.035f, 0.075f, abs(materialClass - 0.14f));
    float greenClass =
        1.0f - smoothstep(0.020f, 0.055f, abs(materialClass - 0.06f));
    float shortTurfClass = max(fairwayClass, greenClass);
    float3 viewDirection = normalize(CameraPos.xyz - worldPos.xyz);
    float overheadRatio = abs(viewDirection.y);
    float overheadFade = 1.0f - smoothstep(0.55f, 0.75f, overheadRatio);
    output.distanceFade =
        distanceFade * lerp(1.0f, overheadFade, shortTurfClass);
    output.materialClass = materialClass;
    return output;
}
