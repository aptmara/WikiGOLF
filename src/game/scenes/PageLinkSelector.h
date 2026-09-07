#pragma once

/**
 * @file PageLinkSelector.h
 * @brief 記事本文からプレイ対象リンクを選択する規則を定義します。
 */

#include "../systems/WikiClient.h"
#include <string>
#include <utility>
#include <vector>

namespace game::scenes {

/** @brief 選択されたリンクと目的地追加結果です。 */
struct PageLinkSelectionResult {
    std::vector<std::pair<std::string, std::wstring>> links;
    bool targetAdded = false;
};

/**
 * @brief 記事内リンクの除外、本文順整列、不足補充を担当します。
 */
class PageLinkSelector {
public:
    /**
     * @brief プレイ対象リンクを選択します。
     * @param allLinks Wikipedia APIから取得したリンクです。
     * @param articleText 本文と表情報を連結した記事テキストです。
     * @param targetPage 目的記事名です。
     */
    PageLinkSelectionResult Select(
        const std::vector<game::WikiLink>& allLinks,
        const std::string& articleText,
        const std::string& targetPage) const;

private:
    /** @brief 年月日または数字だけのリンクを除外します。 */
    bool IsIgnored(const std::string& title) const;
};

} // namespace game::scenes
