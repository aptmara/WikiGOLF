Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

cbuffer ConstantBuffer : register(b0) {
    float4 Color;      // フェード色 (RGB + Aは基本無視だがColor.aも乗算可)
    float Progress;    // 0.0(全描画) -> 1.0(全隠蔽)
    int Type;          // 0: Fade, 1: Circle, 2: Hexagon
    float AspectRatio; // 画面幅/高さ
    float2 Center;     // ワイプ中心 (0.5, 0.5) が中央
    float Smoothness;  // エッジのぼかし
    float Padding;
}

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

// ヘキサゴン距離関数
// uv: 座標
// radius: 半径（進行度）
float HexagonDist(float2 uv) {
    // 六角形グリッド計算
    float2 q = abs(uv);
    return max(q.x * 0.866025 + q.y * 0.5, q.y); // cos(30), sin(30)
}

float4 main(VS_OUTPUT input) : SV_Target {
    float alpha = 1.0f;
    float2 uv = input.Tex;
    
    // UVをアスペクト比補正 (中心基準)
    float2 aspectUV = (uv - Center);
    aspectUV.x *= AspectRatio;
    
    if (Type == 0) { // Standard Fade
        alpha = Progress;
    }
    else if (Type == 1) { // Circle Wipe
        float dist = length(aspectUV);
        // Progress=0 -> radius=Max, Progress=1 -> radius=0
        // 画面全体を覆うのに十分な最大半径 (対角線など)
        float maxRadius = 1.5f; 
        float radius = maxRadius * (1.0f - Progress);
        
        // エッジぼかし: smoothstep(radius, radius + smooth, dist) など
        // ここでは「黒で塗りつぶす」= alpha=1 となる領域を計算
        // radiusより外側が alpha=1 (黒)、内側が alpha=0 (透明)
        
        alpha = smoothstep(radius, radius - Smoothness, dist);
        // 上記だと radiusより遠い(dist大)と 0, 近いと1 になるので逆
        // 内側が見える = alpha=0
        
        alpha = 1.0f - smoothstep(radius, radius + Smoothness, dist);
    }
    else if (Type == 2) { // Hexagon Wipe (Grid)
        // 画面を六角形セルに分割して個別に消していくのは難しいので、
        // シンプルに「中央から巨大な六角形が広がる/閉じる」か、
        // 「グリッド状にランダム/順序立てて消える」か。
        // ここでは「中央から六角形」パターンを採用。
        
        // ヘキサゴン距離
        float dist = HexagonDist(aspectUV);
        float maxRadius = 1.5f;
        float radius = maxRadius * (1.0f - Progress);
        
        alpha = 1.0f - smoothstep(radius, radius + Smoothness, dist);
    }
    
    // アルファを適用して色を出力
    // Overlapなので、alpha=1でColor色、alpha=0で透明(discard or blend zero)
    // ブレンドステートで SrcAlpha, InvSrcAlpha を想定
    
    return float4(Color.rgb, alpha);
}
