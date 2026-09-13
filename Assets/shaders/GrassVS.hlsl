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
    matrix ShadowViewProjection;
    float4 ShadowParams;
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
    float4 shadowPosition : TEXCOORD4;
};

/**
 * @brief 2D座標から疑似乱数を生成します。
 * @param p 入力座標
 * @return 0～1の乱数値
 */
float Hash21(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

/**
 * @brief 芝生頂点シェーダーメインエントリ
 * @param input 頂点入力情報
 * @return 風・曲がり変形適用後の頂点出力
 */
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
    float responseRadius = lerp(0.30f, 0.55f, materialClass);
    float contactFalloff = saturate(1.0f - length(fromContact) / responseRadius);
    contactFalloff = contactFalloff * contactFalloff * (3.0f - 2.0f * contactFalloff);
    float bend = saturate(inst.Flags.w) * contactFalloff * tipWeight;
    float2 bendDirection = float2(cos(inst.Flags.z), sin(inst.Flags.z));
    float flexibility = materialClass * materialClass;
    float displacement = lerp(0.01f, 0.30f, flexibility) * bend;
    worldPos.xz += bendDirection * displacement;
    worldPos.y -= lerp(0.01f, 0.16f, flexibility) * bend * bend;

    // ゲーム内の風向・風速へ連動する。遠くまで伝わる風の帯、局所的な
    // 突風、葉ごとの細かな揺れを異なる周期で重ね、芝面全体が同時に
    // 左右へ往復して見えないようにする。
    float time = LightDir.w;
    float2 windVector = MaterialFlags_unused.xy;
    float windLength = length(windVector);
    float2 windDir = windLength > 0.0001f
        ? windVector / windLength
        : float2(0.62f, 0.78f);
    float2 crossWindDir = float2(-windDir.y, windDir.x);
    float windStrength = saturate(MaterialFlags_unused.z / 12.0f);
    float travel = dot(worldPos.xz, windDir);

    // 低周波の帯は近くの葉をまとまって傾け、高周波成分は一枚ごとの
    // ばらつきを作る。gustEnvelopeは0～1に保ち、強風時だけ風下への
    // 平均的な傾きを加える。
    float broadWave = sin(travel * 0.16f - time *
                          (0.72f + windStrength * 1.05f));
    float gustWave = sin(travel * 0.43f - time *
                         (1.18f + windStrength * 1.85f) + windPhase * 0.22f);
    float gustEnvelope = saturate(0.52f + broadWave * 0.30f +
                                   gustWave * 0.18f);
    gustEnvelope = gustEnvelope * gustEnvelope *
                   (3.0f - 2.0f * gustEnvelope);

    float bladeFlutter =
        sin(travel * 1.55f + time * (1.70f + windStrength * 3.10f) +
            windPhase) * 0.68f +
        sin(travel * 2.85f - time * (2.35f + windStrength * 4.20f) +
            windPhase * 1.73f) * 0.32f;
    float crossFlutter =
        sin(travel * 1.10f + time * (1.25f + windStrength * 2.40f) +
            windPhase * 1.31f);

    float windActivity = smoothstep(0.015f, 0.12f, windStrength);
    float windAmplitude = lerp(0.004f, 0.115f, windStrength);
    float surfaceFlex = 0.08f + materialClass * 0.92f;
    float windTipWeight = tipWeight * tipWeight * (3.0f - 2.0f * tipWeight);
    float downwindLean = windAmplitude * windActivity *
                         (0.18f + gustEnvelope * 0.48f);
    float alongWindSway =
        downwindLean + bladeFlutter * windAmplitude *
        lerp(0.42f, 0.62f, gustEnvelope);
    float crossWindSway = crossFlutter * windAmplitude * 0.18f *
                          (0.35f + gustEnvelope * 0.65f);
    float2 windOffset =
        (windDir * alongWindSway + crossWindDir * crossWindSway) *
        surfaceFlex * windTipWeight;

    // 疎な局所風を風下へ流す。同じ横帯に次の塊が来るまで十分な距離を
    // 空け、packetRandomで一部を間引くことで、同じ地点では時折だけ
    // つむじ風が通過する。帯の境界では影響半径が届かないため継ぎ目は
    // 表面に現れない。
    float alongPosition = dot(worldPos.xz, windDir);
    float crossPosition = dot(worldPos.xz, crossWindDir);
    const float vortexBandSpacing = 24.0f;
    const float vortexPacketSpacing = 78.0f;
    float vortexBand = floor(crossPosition / vortexBandSpacing);
    float bandRandom = Hash21(float2(vortexBand, 37.19f));
    float vortexCrossCenter =
        (vortexBand + 0.5f) * vortexBandSpacing +
        (bandRandom - 0.5f) * 5.0f;
    float vortexTravel = time * lerp(2.2f, 5.2f, windStrength);
    float packetCoordinate =
        alongPosition - vortexTravel + bandRandom * vortexPacketSpacing;
    float vortexPacket = floor(packetCoordinate / vortexPacketSpacing);
    float localAlong =
        (frac(packetCoordinate / vortexPacketSpacing) - 0.5f) *
        vortexPacketSpacing;
    float localCross = crossPosition - vortexCrossCenter;
    float packetRandom = Hash21(float2(vortexBand + 83.7f,
                                       vortexPacket - 19.4f));
    float packetExists = smoothstep(0.28f, 0.38f, packetRandom);

    float vortexAlongRadius = lerp(5.5f, 8.0f, windStrength);
    float vortexCrossRadius = lerp(3.2f, 4.8f, windStrength);
    float vortexDistance = length(float2(localAlong / vortexAlongRadius,
                                         localCross / vortexCrossRadius));
    float vortexMask = saturate(1.0f - vortexDistance);
    vortexMask = vortexMask * vortexMask *
                 (3.0f - 2.0f * vortexMask) * packetExists * windActivity;

    float2 fromVortex =
        windDir * localAlong + crossWindDir * localCross;
    float fromVortexLength = max(length(fromVortex), 0.001f);
    float2 radialDirection = fromVortex / fromVortexLength;
    float spinDirection = bandRandom < 0.5f ? -1.0f : 1.0f;
    float2 vortexTangent =
        float2(-radialDirection.y, radialDirection.x) * spinDirection;
    float vortexRing = saturate(vortexDistance * 2.4f);
    float vortexAmplitude = lerp(0.035f, 0.155f, windStrength);
    float2 vortexOffset =
        (vortexTangent * lerp(0.35f, 1.0f, vortexRing) + windDir * 0.42f) *
        vortexAmplitude * vortexMask * surfaceFlex * windTipWeight;
    windOffset += vortexOffset;

    worldPos.xz += windOffset;
    worldPos.y -= length(windOffset) * lerp(0.08f, 0.22f, windStrength);

    output.position = mul(mul(worldPos, View), Projection);

    // SRT行列の各基底からスケールを除いて法線を変換する。草丈と横幅で
    // 非等方スケールしても、照明法線が押し潰されないようにする。
    float3x3 world3x3 = (float3x3)inst.World;
    float3x3 rotationOnly = float3x3(normalize(world3x3[0]),
                                     normalize(world3x3[1]),
                                     normalize(world3x3[2]));
    float3 normal = normalize(mul(input.normal, rotationOnly));
    float2 leanTotal = bendDirection * bend * 0.85f + windOffset * 3.6f;
    normal = normalize(normal + float3(-leanTotal.x,
                                       (bend + length(windOffset)) * 0.3f,
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
    // ラフ／セミラフも同様に、パッチ単位の株がほぼ真上から見ると
    // 地面に貼り付いた正方形の塊として点在して見えてしまう。丈があり
    // 通常のプレイ視点では立体感を保ちたいため、フェアウェイよりかなり
    // 急な見下ろし角（ほぼ真上）でだけ地形側の短芝表現へ切り替える。
    float roughClass =
        1.0f - smoothstep(0.15f, 0.25f, abs(materialClass - 0.73f));
    float3 viewDirection = normalize(CameraPos.xyz - worldPos.xyz);
    float overheadRatio = abs(viewDirection.y);
    float shortTurfOverheadFade = 1.0f - smoothstep(0.55f, 0.75f, overheadRatio);
    float roughOverheadFade = 1.0f - smoothstep(0.80f, 0.93f, overheadRatio);
    float overheadFade = 1.0f;
    overheadFade = lerp(overheadFade, shortTurfOverheadFade, shortTurfClass);
    overheadFade = lerp(overheadFade, roughOverheadFade, roughClass);
    output.distanceFade = distanceFade * overheadFade;
    output.materialClass = materialClass;
    output.shadowPosition = mul(worldPos, ShadowViewProjection);
    return output;
}
