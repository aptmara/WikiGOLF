/**
 * @file TemporalVelocity.hlsli
 * @brief TAA/DLSS用の画面空間モーションベクター（速度バッファ）出力の共通処理
 * @details シーン描画は SV_Target0 にカラー、SV_Target1 に速度を書く。
 *          速度は「今フレームのUV − 前フレームのUV」（ジッター除去済み、UV単位）。
 *          a=1 は物体自身の動きを含む速度、a=0 は「カメラ移動のみ」を意味し、
 *          解決パス(VelocityResolvePS)で深度から再投影した値に置き換えられる。
 */

cbuffer TemporalConstants : register(b6) {
    matrix PrevViewProjection; /**< 前フレームのView*Projection（ジッター無し） */
    float4 TemporalJitterUv;   /**< xy: 今フレームのジッター(UV)、zw: 1/描画解像度 */
    float4 TemporalParams;     /**< x: 前フレームの経過時間[秒]、y: 速度出力有効(1/0) */
};

/** @brief カメラ移動のみ（静止物）を表す速度。解決パスで深度から補完される */
static const float4 kCameraOnlyVelocity = float4(0.0f, 0.0f, 0.0f, 0.0f);

/** @brief 前フレームのワールド座標をクリップ空間へ変換する */
float4 TemporalPrevClip(float3 prevWorldPosition) {
    return mul(float4(prevWorldPosition, 1.0f), PrevViewProjection);
}

/**
 * @brief 画素位置と前フレームのクリップ座標から速度を作る
 * @param svPosition ピクセルシェーダーの SV_POSITION（ジッター付き）
 * @param prevClip 頂点シェーダーで求めた前フレームのクリップ座標
 */
float4 EncodeVelocity(float4 svPosition, float4 prevClip) {
    if (TemporalParams.y < 0.5f || prevClip.w <= 0.0001f) {
        return kCameraOnlyVelocity;
    }
    float2 currentUv = svPosition.xy * TemporalJitterUv.zw - TemporalJitterUv.xy;
    float2 prevNdc = prevClip.xy / prevClip.w;
    float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
    return float4(currentUv - prevUv, 0.0f, 1.0f);
}

/** @brief シーン描画用ピクセルシェーダーの出力（カラー + 速度） */
struct SceneOutput {
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
};

SceneOutput MakeSceneOutput(float4 color, float4 velocity) {
    SceneOutput output;
    output.color = color;
    output.velocity = velocity;
    return output;
}
