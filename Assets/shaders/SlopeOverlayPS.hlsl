/**
 * @file SlopeOverlayPS.hlsl
 * @brief 傾斜可視化オーバーレイ用ピクセルシェーダー
 * @details 頂点カラーの傾斜強度から青→赤のグラデーションを描画しつつ、
 *          傾斜が下る方向へ流れるシェブロン（矢印）状の縞模様を重ねる。
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags; /**< z成分にフェード係数[0,1](customFlags.x経由)が格納される */
    float4 LightDir;      /**< w成分にゲーム経過時間(ctx.time)が格納される */
};

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 position : SV_POSITION;
    float4 color : COLOR;        /**< rgb=傾斜カラー(青→赤), a=傾斜強度[0,1] */
    float3 flowDir : TEXCOORD0;  /**< ワールド空間の傾斜下り方向 */
    float2 worldXZ : TEXCOORD1;  /**< ワールドXZ座標 */
};

/**
 * @brief 傾斜オーバーレイピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 合成済みピクセルカラー（半透明）
 */
float4 main(PS_INPUT input) : SV_TARGET {
    float slope = saturate(input.color.a);
    float time = LightDir.w;
    float fade = saturate(MaterialFlags.z);

    // 傾斜が下る方向の単位ベクトルと、それに直交するベクトル
    float2 dir = input.flowDir.xz;
    float dirLen = length(dir);
    dir = (dirLen > 0.0001f) ? (dir / dirLen) : float2(0.0f, 1.0f);
    float2 perp = float2(-dir.y, dir.x);

    float along = dot(input.worldXZ, dir);
    float across = dot(input.worldXZ, perp);

    // シェブロン（矢印）状の縞：across方向にずらした位相で三角波を作り、
    // 下る方向(dir)へ向かって時間経過でスクロールさせる。
    const float kFrequency = 1.4f;   // 1mあたりの縞の数
    const float kSkew = 0.55f;       // 矢印の開き角
    const float kSpeed = 1.3f;       // 流れる速さ

    float phase = frac(along * kFrequency - abs(across) * kSkew - time * kSpeed);
    float triWave = 1.0f - abs(phase * 2.0f - 1.0f); // 0→1→0の三角波
    float arrow = smoothstep(0.55f, 0.97f, triWave);

    // 傾斜が緩やかな場所は縞を目立たせず、急な場所ほど強く流す
    float baseAlpha = lerp(0.08f, 0.50f, slope);
    float flowAlpha = arrow * lerp(0.0f, 0.85f, slope);
    float alpha = saturate(baseAlpha + flowAlpha) * fade;

    return float4(input.color.rgb, alpha);
}
