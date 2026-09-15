/**
 * @file UnlitPS.hlsl
 * @brief 発光・非ライティング用ピクセルシェーダー（カップイン・ショット演出グロー等）
 */

cbuffer ConstantBuffer : register(b0) {
    matrix World;          /**< ワールド行列 */
    matrix View;           /**< ビュー行列 */
    matrix Projection;     /**< 射影行列 */
    float4 MaterialColor;  /**< マテリアルベースカラー */
    float4 MaterialFlags;  /**< マテリアルフラグ */
};

#include "TemporalVelocity.hlsli"

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力（BasicVS.hlsl の出力と対応）
 */
struct PS_INPUT {
    float4 position : SV_POSITION;  /**< 射影座標 */
    float3 normal : NORMAL;         /**< ワールド法線 */
    float2 texCoord : TEXCOORD;     /**< UV座標 */
    float4 color : COLOR;           /**< 頂点カラー */
    float3 tangent : TANGENT;       /**< ワールド接線 */
    float3 bitangent : BINORMAL;    /**< ワールド従法線 */
    float2 worldXZ : TEXCOORD1;     /**< ワールドXZ座標 */
    float4 materialFlags : TEXCOORD4;/**< 未使用 */
    float4 shadowPosition : TEXCOORD5;/**< 未使用 */
    float3 worldPosition : TEXCOORD6;/**< 未使用 */
    float cupClip : TEXCOORD7;      /**< 未使用 */
    float ditherFade : TEXCOORD8;   /**< 未使用 */
    float4 prevClip : TEXCOORD9;    /**< 前フレームのクリップ座標 */
};

/**
 * @brief 発光ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return リムグロー適用済み発光カラーと速度
 */
SceneOutput main(PS_INPUT input) {
    // ビュー空間法線へ変換
    float3 viewNormal = normalize(mul((float3x3)View, input.normal));

    // カメラ正対面で1.0、シルエット部で0.0となるグロー係数
    float centerGlow = saturate(abs(viewNormal.z));
    float glow = pow(centerGlow, 3.0f);

    return MakeSceneOutput(float4(MaterialColor.rgb, MaterialColor.a * glow),
                           EncodeVelocity(input.position, input.prevClip));
}
