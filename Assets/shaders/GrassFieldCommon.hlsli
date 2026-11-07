/**
 * @file GrassFieldCommon.hlsli
 * @brief 手続き的に生やす芝の共通定義（定数、ハッシュ、フィールドの読み取り）
 * @details 定数の並びは GrassFieldRenderer.cpp の GrassFrameConstants / GrassDrawConstants と同じ。
 *          密度の式は GrassFieldRules.h / GrassFieldBuilder.cpp と同じ。
 */

#ifndef GRASS_FIELD_COMMON_HLSLI
#define GRASS_FIELD_COMMON_HLSLI

#define GRASS_LAYER_ROUGH 0
#define GRASS_LAYER_TURF 1
#define GRASS_MAX_LOD_COUNT 8
#define GRASS_LEAVES_PER_SHOOT 3
#define GRASS_TILE_NEAR_CUP 1u

cbuffer GrassFrame : register(b5) {
    matrix GrassViewProjection;  /**< ジッター込み、転置済み */
    matrix GrassShadowViewProjection;
    float4 GrassCamera;          /**< xyz: カメラ位置、w: フォグ有効(1/0) */
    float4 GrassLight;           /**< xyz: 光の向き、w: 経過時間[秒] */
    float4 GrassShadowParams;    /**< x: 影有効(1/0) */
    float4 GrassFieldRect;       /**< xy: フィールドの最小 x,z、zw: 幅・奥行き */
    float4 GrassFieldInfo;       /**< x: 高さのオフセット、y: 前フレームの経過時間、z: 前フレームからの秒数 */
    int4 GrassHeightInfo;        /**< xy: 高さマップの解像度 */
    float4 GrassWindOrigin;      /**< xy: 風テクスチャの原点、zw: 前フレームの原点 */
    float4 GrassWindParams;      /**< xy: 風下の向き、z: 風の強さ(0〜1)、w: 風速[m/s] */
    float4 GrassTrampleOrigin;   /**< xy: 踏み跡の窓の原点、zw: 前フレームの原点 */
    float4 GrassTrampleBall;     /**< xyz: ボール位置、w: 接地(1/0) */
    float4 GrassTrampleBallPrev; /**< xyz: 前フレームのボール位置、w: 押し倒す半径 */
    float4 GrassTrampleMotion;   /**< xy: ボールの水平速度、z: 減衰率 */
    float4 GrassFrustum[6];      /**< ジッター無しの視錐台平面（ワールド） */
    float4 GrassLayerA[2];       /**< x: タイルの一辺、y: r0、z: 最大距離、w: 消えていく幅 */
    float4 GrassLayerB[2];       /**< x: 葉の最大の高さ、y: LOD 段数、z: LOD0 のシュート数、w: 有効(1/0) */
    int4 GrassLayerWindow[2];    /**< xy: 窓の最初のタイル、zw: 窓のタイル数 */
    int4 GrassLayerGrid[2];      /**< xy: タイルの数、z: 1区画の容量 */
};

cbuffer GrassDraw : register(b7) {
    uint DrawLayer;
    uint DrawLod;
    uint DrawRegionBase;    /**< 見えるタイル一覧での区画の先頭 */
    uint DrawVertsPerBlade;
};

static const float kGrassFieldCell = 0.4f;
static const float kWindTexelSize = 1.0f;
static const float kWindResolution = 128.0f;
static const float kTrampleTexelSize = 0.0625f;
static const float kTrampleResolution = 256.0f;

/** @brief 整数ハッシュ（PCG） */
uint GrassHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/** @brief ハッシュを 0〜1 の小数にする */
float GrassHashToFloat(uint h) {
    return float(h >> 8u) * (1.0f / 16777216.0f);
}

uint GrassTileSeed(int2 tile, uint layer) {
    return GrassHash(uint(tile.x) * 73856093u ^ uint(tile.y) * 19349663u ^
                     (layer + 1u) * 83492791u);
}

/** @brief タイルの最小の角（ワールド xz） */
float2 GrassTileOrigin(int2 tile, uint layer) {
    return GrassFieldRect.xy + float2(tile) * GrassLayerA[layer].x;
}

/** @brief ワールド xz からフィールドテクスチャの UV（行 0 がフィールドの奥 = z 最大） */
float2 GrassFieldUv(float2 xz) {
    return float2((xz.x - GrassFieldRect.x) / GrassFieldRect.z,
                  (GrassFieldRect.y + GrassFieldRect.w - xz.y) / GrassFieldRect.w);
}

/** @brief ラフ層の密度（GrassFieldBuilder.cpp の GrassRoughLayerDensity と同じ） */
float GrassRoughDensity(float4 texel) {
    return saturate(texel.r / 0.2f) * texel.a;
}

/** @brief 短い芝の層の密度（GrassFieldBuilder.cpp の GrassTurfLayerDensity と同じ） */
float GrassTurfDensity(float4 texel) {
    return saturate(texel.g) * texel.a;
}

/** @brief 全密度を保つ距離 r0 より遠くの目標密度（GrassDensityFraction と同じ） */
float GrassDensityFraction(float distance, float fullDensityDistance) {
    if (distance <= fullDensityDistance) {
        return 1.0f;
    }
    float ratio = fullDensityDistance / distance;
    return ratio * ratio;
}

/** @brief 風テクスチャの UV */
float2 GrassWindUv(float2 xz, float2 origin) {
    return (xz - origin) / (kWindTexelSize * kWindResolution);
}

#endif
