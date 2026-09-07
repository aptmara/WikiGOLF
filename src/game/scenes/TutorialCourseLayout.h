#pragma once

/**
 * @file TutorialCourseLayout.h
 * @brief チュートリアル専用コースの固定配置規則を定義します。
 */

#include "../../graphics/WikiTextureGenerator.h"
#include <string>
#include <vector>

namespace game::scenes {

/**
 * @brief チュートリアル記事を一定の教材コースへ変換します。
 *
 * 記事本文の描画結果は保ち、プレイ用リンクの種類と位置だけを固定します。
 * 座標変換をこのクラスへ集約し、同期構築と段階構築で同じ規則を使います。
 */
class TutorialCourseLayout {
public:
    /** @brief 固定教材コースの幅を返します。 */
    static constexpr float GetFieldWidth() { return 96.0f; }

    /** @brief 固定教材コースの奥行きを返します。 */
    static constexpr float GetFieldDepth() { return 144.0f; }

    /**
     * @brief 指定記事が固定教材コースの対象か判定します。
     * @param pageName Wikipediaの記事名です。
     */
    bool IsPresetPage(const std::string& pageName) const;

    /**
     * @brief 描画済み記事から固定配置のプレイ用リンクを生成します。
     * @param texture 記事テクスチャと元のリンク情報です。
     * @param targetPage 目的地として強調する記事名です。
     * @return 固定順序、固定座標へ変換した6件のリンクです。
     */
    std::vector<graphics::LinkRegion> BuildGameplayLinks(
        const graphics::WikiTextureResult& texture,
        const std::string& targetPage) const;

private:
    /**
     * @brief ワールド座標を記事テクスチャ上のリンク矩形へ変換します。
     */
    void PlaceAtWorld(graphics::LinkRegion& link,
                      const graphics::WikiTextureResult& texture,
                      float worldX, float worldZ) const;
};

} // namespace game::scenes
