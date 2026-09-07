/**
 * @file PageLinkSelector.cpp
 * @brief 記事内リンク選択を実装します。
 */

#include "PageLinkSelector.h"
#include "../../core/StringUtils.h"
#include <algorithm>
#include <cctype>

namespace game::scenes {

PageLinkSelectionResult PageLinkSelector::Select(
    const std::vector<game::WikiLink>& allLinks,
    const std::string& articleText,
    const std::string& targetPage) const {
    PageLinkSelectionResult result;
    std::vector<std::pair<std::size_t, std::string>> articleLinks;

    for (const auto& link : allLinks) {
        if (IsIgnored(link.title) || link.title == targetPage) {
            continue;
        }
        const std::size_t position = articleText.find(link.title);
        if (position != std::string::npos) {
            articleLinks.push_back({position, link.title});
        }
    }

    std::sort(articleLinks.begin(), articleLinks.end(),
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });
    for (const auto& link : articleLinks) {
        result.links.push_back({link.second, core::ToWString(link.second)});
    }

    if (!targetPage.empty() && articleText.find(targetPage) != std::string::npos) {
        const auto existing = std::find_if(
            result.links.begin(), result.links.end(),
            [&targetPage](const auto& link) {
                return link.first == targetPage;
            });
        if (existing == result.links.end()) {
            result.links.push_back({targetPage, core::ToWString(targetPage)});
            result.targetAdded = true;
        }
    }

    if (result.links.size() < 3) {
        for (const auto& link : allLinks) {
            const auto existing = std::find_if(
                result.links.begin(), result.links.end(),
                [&link](const auto& selected) {
                    return selected.first == link.title;
                });
            if (existing == result.links.end() && !IsIgnored(link.title)) {
                result.links.push_back({link.title, core::ToWString(link.title)});
                if (result.links.size() >= 5) {
                    break;
                }
            }
        }
    }
    return result;
}

bool PageLinkSelector::IsIgnored(const std::string& title) const {
    if (title.empty()) {
        return true;
    }
    if (title.size() >= 3) {
        const std::string suffix = title.substr(title.size() - 3);
        if (suffix == "年" || suffix == "月" || suffix == "日") {
            return true;
        }
    }
    return std::all_of(title.begin(), title.end(),
                       [](unsigned char character) {
                           return std::isdigit(character) != 0;
                       });
}

} // namespace game::scenes
