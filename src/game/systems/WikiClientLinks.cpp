/**
 * @file WikiClientLinks.cpp
 * @brief WikiClientの責務別実装です。
 */

#include "WikiClient.h"
#include "WikiClientJson.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>

#pragma comment(lib, "winhttp.lib")

namespace game::systems {

namespace {
constexpr int kWikiLinksBatchSize = 500;
constexpr int kUnlimitedWikiLinkLimit = std::numeric_limits<int>::max();
} // namespace

std::vector<game::WikiLink> WikiClient::FetchPageLinks(const std::string &title,
                                                       int limit) {
  std::vector<game::WikiLink> links;
  std::unordered_set<std::string> seen;

  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  // limit <= 0 は記事内リンクを取り切る。明示 limit は軽量モード用に尊重する。
  int effectiveLimit = limit;
  if (limit <= 0) {
    effectiveLimit = kUnlimitedWikiLinkLimit;
  }

  std::wstring plcontinue = L"";
  bool hasMore = true;
  std::unordered_set<std::wstring> seenContinueTokens;

  while (hasMore && (int)links.size() < effectiveLimit) {
    std::wstring path =
        L"/w/api.php?action=query&titles=" + wtitle + L"&prop=links&pllimit=" +
        std::to_wstring(kWikiLinksBatchSize) +
        L"&plnamespace=0&redirects=1&format=json&formatversion=2";

    if (!plcontinue.empty()) {
      path += L"&plcontinue=" + plcontinue;
    }

    std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
    if (response.empty()) {
      LOG_WARN("WikiClient", "FetchPageLinks got empty response for {}", title);
      break;
    }

    if (links.empty()) {
      LOG_INFO("WikiClient", "FetchPageLinks response length: {}",
               response.length());
    }

    wiki_json::ParseQueryPageLinks(response, title, effectiveLimit, links,
                                   seen);

    size_t contPos = response.find("\"plcontinue\":\"");
    if (contPos != std::string::npos && (int)links.size() < effectiveLimit) {
      std::string continueValue;
      size_t nextPos = 0;
      if (wiki_json::ExtractStringField(response, "\"plcontinue\":\"", contPos,
                                        response.length(), continueValue,
                                        nextPos)) {
        plcontinue = core::ToWString(UrlEncode(continueValue));
        if (!seenContinueTokens.insert(plcontinue).second) {
          LOG_WARN("WikiClient",
                   "FetchPageLinks stopped repeated continuation for {}",
                   title);
          hasMore = false;
        }
      } else {
        hasMore = false;
      }
    } else {
      hasMore = false;
    }
  }

  const size_t queryLinkCount = links.size();
  if ((int)links.size() < effectiveLimit) {
    std::wstring parsePath =
        L"/w/api.php?action=parse&page=" + wtitle +
        L"&prop=links&redirects=1&format=json&formatversion=2";
    std::string response = PerformGetRequest(L"ja.wikipedia.org", parsePath);
    wiki_json::ParseRenderedPageLinks(response, title, effectiveLimit, links,
                                      seen);
  }

  LOG_INFO("WikiClient", "Found {} links (query={}, rendered={})",
           links.size(), queryLinkCount, links.size() - queryLinkCount);
  return links;
}

std::vector<std::string>
WikiClient::FetchPageCategories(const std::string &title) {
  std::vector<std::string> categories;
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  std::wstring path =
      L"/w/api.php?action=query&titles=" + wtitle +
      L"&prop=categories&cllimit=50&format=json&formatversion=2";

  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);

  size_t catStart = response.find("\"categories\":");
  if (catStart == std::string::npos) {
    return categories;
  }

  size_t catEnd = response.find("]", catStart);
  if (catEnd == std::string::npos)
    catEnd = response.length();

  size_t pos = catStart;
  while ((pos = response.find("\"title\":\"", pos)) != std::string::npos) {
    if (pos > catEnd)
      break;

    size_t start = pos + 9;
    size_t end = response.find("\"", start);
    if (end == std::string::npos)
      break;

    std::string catTitle = response.substr(start, end - start);
    catTitle = wiki_json::DecodeUnicodeEscape(catTitle);

    categories.push_back(catTitle);
    pos = end;
  }

  LOG_INFO("WikiClient", "Fetched {} categories for {}", categories.size(),
           title);
  return categories;
}

} // namespace game::systems

