/**
 * @file TransitionPS.hlsl
 * @brief シーン遷移エフェクト用ピクセルシェーダー（フェード、サークルワイプ、ヘキサゴンワイプ、アイリス）
 */

Texture2D txDiffuse : register(t0);       /**< ディフューズテクスチャ（オプション） */
SamplerState samLinear : register(s0);    /**< サンプラーステート */

// C++側 ScreenFade::FadeCB (XMFLOAT4 x4) と1対1で対応させる
cbuffer ConstantBuffer : register(b0) {
    float4 Color;         /**< フェードカラー (RGB + A) */
    float Progress;       /**< 進行度 (0.0:全描画 ～ 1.0:全隠蔽) */
    float Type;           /**< 遷移種別 (0:フェード, 1:サークル, 2:ヘキサゴン, 3:アイリス) */
    float AspectRatio;    /**< 画面アスペクト比（幅/高さ） */
    float Smoothness;     /**< エッジのぼかし幅 */
    float2 Center;        /**< ワイプ中心UV座標（左上原点） */
    float IrisHoldRadius; /**< アイリスが溜める円の半径（画面高さ=1基準） */
    float Padding0;
    float2 ViewportSize;  /**< 描画先ビューポートサイズ（ピクセル） */
    float2 Padding1;
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

/** @brief 中心から画面の最も遠い角までの距離（高さ=1基準）*/
float MaxCornerDistance() {
    float2 farthest = max(Center, 1.0f - Center);
    farthest.x *= AspectRatio;
    return length(farthest);
}

/** @brief 閉じていく図形の外側を塗るアルファ（radius<=-edgeで全面）*/
float WipeAlpha(float dist, float radius, float edge) {
    return smoothstep(radius, radius + edge, dist);
}

/**
 * @brief アイリスの半径を求めます。
 * @details 画面端→溜め半径まで素早く寄り、少し溜めてから完全に閉じる。
 */
float IrisRadius(float maxRadius, float edge) {
    float hold = max(IrisHoldRadius, 0.0f);
    if (hold <= 0.0f) {
        float t = saturate(Progress);
        return lerp(maxRadius, -edge, t * t);
    }
    if (Progress < 0.55f) {
        float t = saturate(Progress / 0.55f);
        float e = 1.0f - pow(1.0f - t, 3.0f);
        return lerp(maxRadius, hold, e);
    }
    if (Progress < 0.8f) {
        return hold;
    }
    float t = saturate((Progress - 0.8f) / 0.2f);
    return lerp(hold, -edge, t * t);
}

/**
 * @brief シーン遷移ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 遷移アルファ適用済みフェードカラー
 */
float4 main(VS_OUTPUT input) : SV_Target {
    const int type = (int)round(Type);
    if (type == 0) {
        return float4(Color.rgb, saturate(Progress));
    }

    // UV向きがメッシュ依存にならないようピクセル座標から求める（左上原点）
    float2 uv = input.Pos.xy / max(ViewportSize, float2(1.0f, 1.0f));
    float2 d = uv - Center;
    d.x *= AspectRatio;

    float edge = max(Smoothness, 0.001f);
    float maxRadius = MaxCornerDistance() + edge;

    if (type == 1) {
        float radius = lerp(maxRadius, -edge, saturate(Progress));
        return float4(Color.rgb, WipeAlpha(length(d), radius, edge));
    }
    if (type == 2) {
        // 六角形距離は円より小さくなるため、角まで覆える半径に補正する
        float radius = lerp(maxRadius * 1.16f, -edge, saturate(Progress));
        return float4(Color.rgb, WipeAlpha(HexagonDist(d), radius, edge));
    }
    if (type == 3) {
        const float irisEdge = 0.004f;
        float radius = IrisRadius(MaxCornerDistance() + irisEdge, irisEdge);
        return float4(Color.rgb, WipeAlpha(length(d), radius, irisEdge));
    }
    return float4(Color.rgb, saturate(Progress));
}
