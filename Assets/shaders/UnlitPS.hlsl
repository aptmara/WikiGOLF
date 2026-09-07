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

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 position : SV_POSITION;  /**< 射影座標 */
    float3 normal : NORMAL;         /**< ワールド法線 */
    float2 texCoord : TEXCOORD;     /**< UV座標 */
    float4 color : COLOR;           /**< 頂点カラー */
    float3 tangent : TANGENT;       /**< ワールド接線 */
    float3 bitangent : BINORMAL;    /**< ワールド従法線 */
    float2 worldXZ : TEXCOORD1;     /**< ワールドXZ座標 */
};

/**
 * @brief 発光ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return リムグロー適用済み発光カラー
 */
float4 main(PS_INPUT input) : SV_TARGET {
    // ビュー空間法線へ変換
    float3 viewNormal = normalize(mul((float3x3)View, input.normal));
    
    // カメラ正対面で1.0、シルエット部で0.0となるグロー係数
    float centerGlow = saturate(abs(viewNormal.z));
    float glow = pow(centerGlow, 3.0f);
    
    return float4(MaterialColor.rgb, MaterialColor.a * glow);
}
