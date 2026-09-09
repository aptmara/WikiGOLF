/**
 * @file WallPS.hlsl
 * @brief ステージ外周バリア用ピクセルシェーダー
 *
 * 半透明の壁面に、7セグメント風のプロシージャルなグリフ（文字）を
 * SDFで描画し、Wiki記事のコードのような文字列がたまに上から下へ
 * 薄く流れ落ちる演出を重ねる。
 */

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 Pos : SV_POSITION;     /**< 射影座標 */
    float2 TexCoord : TEXCOORD0;  /**< UV座標 */
    float4 Color : COLOR;         /**< マテリアルカラー */
    float3 WorldPos : TEXCOORD1;  /**< ワールド座標 */
    float Time : TEXCOORD2;       /**< 演出用経過時間[秒] */
    float3 WorldNormal : NORMAL;  /**< ワールド法線 */
};

/**
 * @brief 2D座標から擬似乱数ハッシュ値を生成します。
 * @param p 入力座標
 * @return 0～1の乱数値
 */
float hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

/**
 * @brief 軸並行ボックスまでの符号付き距離を求めます。
 * @param p 評価点
 * @param c ボックス中心
 * @param h ボックス半径（半幅・半高さ）
 * @return 符号付き距離（内側は負）
 */
float sdBox(float2 p, float2 c, float2 h) {
    float2 d = abs(p - c) - h;
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

/**
 * @brief 7セグメント風のストロークを組み合わせ、疑似的な文字グリフを描画します。
 * @param uv セル内ローカル座標（0～1）
 * @param seed グリフの形を決めるシード値
 * @return グリフのグロー強度（0～1）
 */
float DrawGlyph(float2 uv, float seed) {
    // 7セグメントディスプレイの配置（上, 左上, 右上, 中央, 左下, 右下, 下）
    const float2 segCenter[7] = {
        float2(0.50f, 0.86f), float2(0.20f, 0.66f), float2(0.80f, 0.66f),
        float2(0.50f, 0.50f), float2(0.20f, 0.34f), float2(0.80f, 0.34f),
        float2(0.50f, 0.14f),
    };
    const float2 segHalf[7] = {
        float2(0.26f, 0.05f), float2(0.05f, 0.15f), float2(0.05f, 0.15f),
        float2(0.24f, 0.05f), float2(0.05f, 0.15f), float2(0.05f, 0.15f),
        float2(0.26f, 0.05f),
    };

    float glow = 0.0f;
    int litCount = 0;
    [unroll]
    for (int i = 0; i < 7; ++i) {
        float bit = hash21(float2(seed * 91.71f + i * 13.13f, seed * 7.31f - i * 5.71f));
        if (bit > 0.55f) {
            ++litCount;
            float d = sdBox(uv, segCenter[i], segHalf[i]);
            glow = max(glow, 1.0f - smoothstep(0.0f, 0.05f, d));
        }
    }
    // 全消灯パターンは「空白文字」として扱う
    return litCount > 0 ? glow : 0.0f;
}

/**
 * @brief 外周バリア ピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return バリア色にコードストリーム演出を重ねたピクセルカラー
 */
float4 main(PS_INPUT input) : SV_TARGET {
    // 外周壁は厚みを持つ箱として配置されているため、そのままでは
    // コース外側からも外向きの面が見えてしまう。コース中心
    // （フィールドはワールド原点を中心に配置される）への方向と
    // 法線が逆を向く面＝外向きの面はここで描画そのものを取り止める。
    float3 toCenter = normalize(float3(-input.WorldPos.x, 0.0f, -input.WorldPos.z));
    float3 n = normalize(input.WorldNormal);
    if (dot(n, toCenter) < 0.0f) {
        discard;
    }

    float4 baseColor = input.Color;

    // レーン分割（壁面ごとにワールド座標でずらして4枚の壁が同期しないようにする）
    const float laneCount = 16.0f;
    const float cellRows  = 34.0f;
    float laneOffset = (input.WorldPos.x + input.WorldPos.z) * 0.5f;
    float laneF = input.TexCoord.x * laneCount + laneOffset;
    float lane = floor(laneF);
    float laneSeed = hash21(float2(lane, 7.0f));

    // レーンごとに出現周期・速度をランダム化し、常時ではなく「たまに」流れるようにする
    float period = 5.5f + laneSeed * 7.0f;
    float speed  = 0.45f + laneSeed * 1.1f;
    float cycleT = input.Time * speed / period + laneSeed * 13.1f;
    float cycleIndex = floor(cycleT);
    float cycleFrac  = frac(cycleT);

    float trigger = hash21(float2(lane, cycleIndex));
    bool streaming = trigger < 0.4f;

    // 上端(0)から下端(1)へ向けてストリームの先頭が流れ落ちる
    float y = 1.0f - saturate(input.TexCoord.y);
    float dist = y - cycleFrac;

    float trail = 0.0f;
    if (streaming && dist >= 0.0f) {
        trail = saturate(1.0f - dist * 2.2f);
        trail *= trail; // 先頭が明るく、尾に向かって減衰
    }

    // セル内ローカル座標とセルインデックス（Wiki記事のコードを思わせるグリフ単位）
    float cellYF = y * cellRows;
    float cellY = floor(cellYF);
    float2 cellUV = float2(frac(laneF), frac(cellYF));

    // 数秒おきにグリフを切り替えてちらつかせる
    float flickerSlot = floor(input.Time * (1.4f + laneSeed * 1.6f));
    float glyphSeed = hash21(float2(lane * 3.7f + 11.0f, cellY + flickerSlot * 0.37f));
    float glyphMask = DrawGlyph(cellUV, glyphSeed);

    // ストリームの先頭付近だけ白く輝かせ、尾は基調カラーへ減衰させる
    float head = saturate(1.0f - dist * 6.0f) * streaming;
    float3 streamColor = lerp(float3(0.55f, 0.95f, 1.0f), float3(1.0f, 1.0f, 1.0f), head);

    float glow = trail * glyphMask;
    baseColor.rgb += streamColor * glow * 0.95f;
    baseColor.a = saturate(baseColor.a + glow * 0.6f);

    return baseColor;
}
