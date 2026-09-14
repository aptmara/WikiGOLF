/**
 * @file GolfCupClip.hlsli
 * @brief カップ（穴）の位置で地表をくり抜くための共通定義
 * @details RenderSystem がカメラ近傍のカップを b3 に設定する。
 *          地形・記事オーバーレイ・芝がこの円内を描かないことで、
 *          その下のカップ内壁と、中に落ちたボールが見えるようになる。
 */

#ifndef GOLF_CUP_CLIP_HLSLI
#define GOLF_CUP_CLIP_HLSLI

#define GOLF_CUP_MAX_COUNT 32

cbuffer GolfCupBuffer : register(b3) {
    float4 GolfCups[GOLF_CUP_MAX_COUNT]; /**< xyz: 縁の中心（yは縁の高さ）、w: 開口半径 */
    float4 GolfCupInfo;                  /**< x: 有効なカップ数 */
};

/**
 * @brief ワールド座標がいずれかのカップ開口部の内側にあるかを返します。
 * @param worldPos ワールド座標
 * @param radiusOffset 開口半径に加える余白
 */
bool IsInsideGolfCupOpening(float3 worldPos, float radiusOffset) {
    int count = min((int)GolfCupInfo.x, GOLF_CUP_MAX_COUNT);
    [loop]
    for (int i = 0; i < count; ++i) {
        float2 delta = worldPos.xz - GolfCups[i].xz;
        float radius = max(GolfCups[i].w + radiusOffset, 0.0f);
        if (dot(delta, delta) < radius * radius &&
            abs(worldPos.y - GolfCups[i].y) < 0.75f) {
            return true;
        }
    }
    return false;
}

/**
 * @brief カップ開口部の内側にあるピクセルを破棄します。
 * @param worldPos ワールド座標
 */
void ClipGolfCupOpening(float3 worldPos) {
    if (IsInsideGolfCupOpening(worldPos, 0.0f)) {
        discard;
    }
}

#endif
