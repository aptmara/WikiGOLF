/**
 * @file TransitionPS.hlsl
 * @brief シーン遷移エフェクト用ピクセルシェーダー（フェード、サークルワイプ、ヘキサゴンワイプ）
 */

Texture2D txDiffuse : register(t0);       /**< ディフューズテクスチャ（オプション） */
SamplerState samLinear : register(s0);    /**< サンプラーステート */

cbuffer ConstantBuffer : register(b0) {
    float4 Color;      /**< フェードカラー (RGB + A) */
    float Progress;    /**< 進行度 (0.0:全描画 ～ 1.0:全隠蔽) */
    int Type;          /**< 遷移種別 (0:通常フェード, 1:サークルワイプ, 2:ヘキサゴンワイプ) */
    float AspectRatio; /**< 画面アスペクト比（幅/高さ） */
    float2 Center;     /**< ワイプ中心UV座標 */
    float Smoothness;  /**< エッジのぼかし幅 */
    float Padding;     /**< アライメント用パディング */
}

/**
 * @struct VS_OUTPUT
 * @brief 頂点シェーダー出力 / ピクセルシェーダー入力
 */
struct VS_OUTPUT {
    float4 Pos : SV_POSITION; /**< 射影座標 */
    float2 Tex : TEXCOORD0;   /**< UV座標 */
};

/**
 * @brief 中心からの六角形距離（ヘキサゴン距離関数）を算出します。
 * @param uv 中心原点のUV座標
 * @return 六角形距離
 */
float HexagonDist(float2 uv) {
    float2 q = abs(uv);
    return max(q.x * 0.866025 + q.y * 0.5, q.y);
}

/**
 * @brief シーン遷移ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 遷移アルファ適用済みフェードカラー
 */
float4 main(VS_OUTPUT input) : SV_Target {
    float alpha = 1.0f;
    float2 uv = input.Tex;
    
    // UVをアスペクト比補正 (中心基準)
    float2 aspectUV = (uv - Center);
    aspectUV.x *= AspectRatio;
    
    if (Type == 0) {
        // 通常フェード
        alpha = Progress;
    }
    else if (Type == 1) {
        // サークルワイプ
        float dist = length(aspectUV);
        float maxRadius = 1.5f; 
        float radius = maxRadius * (1.0f - Progress);
        alpha = 1.0f - smoothstep(radius, radius + Smoothness, dist);
    }
    else if (Type == 2) {
        // ヘキサゴンワイプ
        float dist = HexagonDist(aspectUV);
        float maxRadius = 1.5f;
        float radius = maxRadius * (1.0f - Progress);
        alpha = 1.0f - smoothstep(radius, radius + Smoothness, dist);
    }
    
    return float4(Color.rgb, alpha);
}
