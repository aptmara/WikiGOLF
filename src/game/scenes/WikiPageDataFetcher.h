#pragma once

/**
 * @file WikiPageDataFetcher.h
 * @brief Wikipedia記事の通信・テキスト整形・画像取得を担当します。
*/

#include "../systems/WikiClient.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../../core/GameContext.h"
#include <string>
#include <vector>

namespace game::scenes {

/**
 * @brief ページ構築へ渡す取得済み記事データです。
*/
struct PageDataAsyncResult {
    std::string pageName;
    std::string articleText;
    std::string articleHtml;
    std::vector<game::WikiLink> allLinks;
    std::vector<std::string> pageCategories;
    std::vector<graphics::PendingWikiImage> pendingImages;
    bool hasData = false;
};

/**
 * @brief 記事取得と取得前処理を一つの責務として管理します。
*/
class WikiPageDataFetcher {
public:
    /** @brief 次回取得で使う事前取得データを設定します。*/
    void SetPreloadedData(std::vector<game::WikiLink> links,
                          std::string extract,
                          bool skipSupplementalFetch = false);

    /** @brief 記事本文、リンク、カテゴリ、画像を取得します。*/
    PageDataAsyncResult Fetch(const std::string& pageName);

private:
    /** @brief 記事内画像を取得し、見出しと対応付けます。*/
    std::vector<graphics::PendingWikiImage> FetchAndDecodeImages(
        game::systems::WikiClient& wikiClient,
        const std::string& pageName) const;

    bool m_hasPreloadedData = false;
    bool m_skipSupplementalFetch = false;
    std::vector<game::WikiLink> m_preloadedLinks;
    std::string m_preloadedExtract;
};

} // namespace game::scenes
