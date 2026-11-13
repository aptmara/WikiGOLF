#pragma once

/**
 * @file WikiPageDataFetcher.h
 * @brief Wikipedia記事の通信・テキスト整形・画像取得を担当します。
*/

#include "../systems/WikiClient.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../../core/GameContext.h"
#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
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

/** @brief 取得の進み具合（0〜1）を別スレッドから読むための共有値です。*/
using PageFetchProgress = std::shared_ptr<std::atomic<float>>;

/**
 * @brief 記事取得と取得前処理を一つの責務として管理します。
*/
class WikiPageDataFetcher {
public:
    /** @brief 次回取得で使う事前取得データを設定します。*/
    void SetPreloadedData(std::vector<game::WikiLink> links,
                          std::string extract,
                          bool skipSupplementalFetch = false);

    /**
     * @brief 別の場所で取得済みの記事を渡します。
     * @details 同じ記事名の Fetch は通信せずにこの内容を返します。
     */
    void SetPreloadedPage(PageDataAsyncResult page);

    /**
     * @brief 記事本文、リンク、カテゴリ、画像を取得します。
     * @details 直近に取得した記事は覚えておき、同じ記事へ戻るときは通信しません。
     * @param progress 指定すると、取得の進み具合（0〜1）を書き込みます。
     */
    PageDataAsyncResult Fetch(const std::string& pageName,
                              PageFetchProgress progress = nullptr);

    /**
     * @brief 記事を通信で取得します。インスタンスの状態は使いません。
     * @details 記事HTMLが取れればそこからリンク・本文・カテゴリを取り出し、
     *          取れなければAPIのテキスト取得へ切り替えます。
     * @param presetLinks 取得済みのリンクがあれば渡します（テキスト取得のときだけ使います）。
     * @param presetText 取得済みの本文があれば渡します（テキスト取得のときだけ使います）。
     * @param beforeLicenseCheck 指定すると、画像ライセンスの確認をこれが完了するまで
     *        始めません。ライセンスの確認は応答が遅く、直後のAPI呼び出しを待たせる
     *        ことがあるので、呼び出し側の短い問い合わせを先に済ませたいときに使います。
     */
    static PageDataAsyncResult FetchLive(
        const std::string& pageName, PageFetchProgress progress = nullptr,
        std::vector<game::WikiLink> presetLinks = {},
        std::string presetText = {},
        std::shared_future<void> beforeLicenseCheck = {});

    /** @brief 構築に使える内容（リンクと本文）がそろっているかを返します。*/
    static bool IsUsable(const PageDataAsyncResult& page) {
        return page.hasData && !page.allLinks.empty() &&
               !page.articleText.empty();
    }

private:
    /** @brief 記事内画像を取得し、見出しと対応付けます（テキスト取得用）。*/
    static std::vector<graphics::PendingWikiImage> FetchAndDecodeImages(
        game::systems::WikiClient& wikiClient,
        const std::string& pageName);

    /** @brief 取得済みの記事を覚えます。合計が上限を超えたら古いものから忘れます。*/
    void Remember(std::shared_ptr<const PageDataAsyncResult> page);

    struct RememberedPage {
        std::shared_ptr<const PageDataAsyncResult> page;
        std::size_t bytes = 0;
    };

    std::mutex m_mutex; ///< 先読みと本取得が別スレッドから同時に呼ぶため
    bool m_hasPreloadedData = false;
    bool m_skipSupplementalFetch = false;
    std::vector<game::WikiLink> m_preloadedLinks;
    std::string m_preloadedExtract;
    std::vector<RememberedPage> m_rememberedPages; ///< 末尾が最も新しい
};

} // namespace game::scenes
