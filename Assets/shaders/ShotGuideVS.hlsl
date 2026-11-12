/**
 * @file ShotGuideVS.hlsl
 * @brief ショット予測表示（予測線・着地点の印・足元の向き表示）専用の頂点シェーダー
 * @details builtin/quad を、Flags.z の番号に応じて3通りに置く。
 *          - 帯（1, 2, 11）: ワールドのZ軸に沿って伸び、幅方向は常にカメラを向く
 *          - 玉（3）: 常にカメラを向く
 *          - それ以外: ワールド行列のとおり（地面に寝かせた板）
 *          遠くでも細くなりすぎないよう、画面上の最小の大きさを保証する。
 *          番号は src/game/utils/ShotGuideStyle.h の shape と揃える。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View;
    matrix Projection;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
};

/**
 * @struct InstanceData
 * @brief インスタンスデータ（BasicVS と共通のレイアウト）
 */
struct InstanceData {
    matrix World;     /**< ワールド行列 */
    float4 Color;     /**< 色 */
    float4 Flags;     /**< z: 形の番号、w: 形ごとの値（模様の位相など） */
    matrix PrevWorld; /**< 未使用 */
};

StructuredBuffer<InstanceData> g_instances : register(t15);

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    uint instanceID : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;     /**< x: 幅方向 0〜1、y: 長さ方向 0〜1（前が1） */
    float4 color : COLOR;
    float4 shape : TEXCOORD1;  /**< x: 形の番号、y: 形ごとの値、z: 道のり[m]、w: 実際の幅[m] */
};

/** @brief 画面の高さの半分を1としたときの最小の大きさを、その奥行きでの長さへ直す */
float MinWorldSize(float ndcSize, float viewDepth) {
    return ndcSize * max(viewDepth, 0.05) / max(Projection[1][1], 0.01);
}

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    InstanceData inst = g_instances[input.instanceID];
    const float mode = inst.Flags.z;
    const float2 local = input.position.xy; // -0.5〜0.5

    float4 viewPos;
    float arc = 0.0;
    float width = length(inst.World[0].xyz);

    const bool isRibbon = mode < 2.5 || (mode > 10.5 && mode < 11.5);
    const bool isOrb = mode > 2.5 && mode < 3.5;

    if (isRibbon) {
        float4 center = mul(float4(inst.World[3].xyz, 1.0), View);
        float3 axis = mul(float4(inst.World[2].xyz, 0.0), View).xyz;
        float segmentLength = length(inst.World[2].xyz);
        viewPos = center;
        viewPos.xyz += axis * local.y;

        // 画面上での帯の向きに直交する方向へ広げる。両端を投影してから差を取るので、
        // 道すじが視線とほぼ重なる所（落ちてくる区間など）でもねじれない
        float3 tail = center.xyz - axis * 0.5;
        float3 head = center.xyz + axis * 0.5;
        float2 along = axis.xy;
        if (tail.z > 0.05 && head.z > 0.05) {
            along = head.xy / head.z - tail.xy / tail.z;
        }
        float alongLength = length(along);
        along = alongLength > 0.0001 ? along / alongLength : float2(0.0, 1.0);
        float2 across = float2(along.y, -along.x);

        const float minNdc = mode > 10.5 ? 0.007 : 0.0115;
        const float maxNdc = mode > 10.5 ? 0.060 : 0.050;
        width = clamp(width, MinWorldSize(minNdc, viewPos.z),
                      MinWorldSize(maxNdc, viewPos.z));
        viewPos.xy += across * local.x * width;
        arc = inst.Flags.w + (local.y + 0.5) * segmentLength;
    } else if (isOrb) {
        viewPos = mul(float4(inst.World[3].xyz, 1.0), View);
        width = max(width, MinWorldSize(0.020, viewPos.z));
        viewPos.xy += local * width;
    } else {
        // 地面に寝かせた板。遠いときは中心から広げて、最小の大きさを保つ
        float4 centerView = mul(float4(inst.World[3].xyz, 1.0), View);
        float scale = 1.0;
        if (mode < 8.5) {
            scale = max(1.0, MinWorldSize(0.050, centerView.z) / max(width, 0.001));
        }
        float4 worldPos = mul(float4(float3(local * scale, 0.0), 1.0), inst.World);
        viewPos = mul(worldPos, View);
        width *= scale;
    }

    output.position = mul(viewPos, Projection);
    output.uv = local + 0.5;
    output.color = inst.Color;
    output.shape = float4(mode, inst.Flags.w, arc, width);
    return output;
}
