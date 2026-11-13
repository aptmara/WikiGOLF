/**
 * @file WikiClientContent.cpp
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
#include <unordered_map>
#include <unordered_set>

#pragma comment(lib, "winhttp.lib")

namespace game::systems {

std::string WikiClient::FetchPageThumbnail(const std::string &title,
                                           int thumbSize) {
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  std::wstring path = L"/w/api.php?action=query&titles=" + wtitle +
                      L"&prop=pageimages&piprop=thumbnail%7Cname&pithumbsize=" +
                      std::to_wstring(thumbSize) +
                      L"&format=json&formatversion=2";
  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  if (response.empty()) {
    return "";
  }

  std::string thumbSrc;
  std::string imageName;
  size_t nextPos = 0;
  if (!wiki_json::ExtractStringField(response, "\"source\":\"", 0,
                                     response.size(), thumbSrc, nextPos) ||
      !wiki_json::ExtractStringField(response, "\"pageimage\":\"", 0,
                                     response.size(), imageName, nextPos)) {
    return "";
  }

  // 本文の画像と同じ基準を適用する。ライセンスを確認できない画像は表示しない。
  std::vector<WikiImageInfo> images(1);
  images[0].fileTitle = "ファイル:" + imageName;
  FilterImagesByAllowedLicense(images);
  return images.empty() ? "" : thumbSrc;
}

bool WikiClient::FetchPageLengths(const std::vector<std::string> &titles,
                                  std::vector<int> &lengths) {
  lengths.clear();
  constexpr size_t kMaxTitlesPerRequest = 50;
  for (size_t begin = 0; begin < titles.size();
       begin += kMaxTitlesPerRequest) {
    const size_t end = std::min(titles.size(), begin + kMaxTitlesPerRequest);
    std::string joined;
    for (size_t i = begin; i < end; ++i) {
      if (!joined.empty()) {
        joined += "%7C";
      }
      joined += UrlEncode(titles[i]);
    }
    const std::wstring path = L"/w/api.php?action=query&prop=info&redirects=1"
                              L"&format=json&formatversion=2&titles=" +
                              core::ToWString(joined);
    const std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
    if (response.find("\"query\"") == std::string::npos) {
      return false;
    }
    const auto chunk = wiki_json::ParsePageInfoLengths(response);
    lengths.insert(lengths.end(), chunk.begin(), chunk.end());
  }
  return true;
}

std::string WikiClient::FetchPageExtract(const std::string &title,
                                         int lengthLimit) {
  return FetchExtract(title, lengthLimit, L"");
}

std::string WikiClient::FetchPagePreview(const std::string &title,
                                         int maxChars) {
  const int chars = std::clamp(maxChars, 1, 1200);
  return FetchExtract(title, chars, L"&exchars=" + std::to_wstring(chars));
}

std::string WikiClient::FetchExtract(const std::string &title, int lengthLimit,
                                     const std::wstring &extraQuery) {
  std::string encodedTitle = UrlEncode(title);

  std::wstring wtitle = core::ToWString(encodedTitle);

  // exsectionformat=wiki を指定することで、プレーンテキスト抽出でも
  // 見出し記法「== 見出し ==」が保持される（見出し解析・強調表示に利用）。
  std::wstring path =
      L"/w/"
      L"api.php?action=query&prop=extracts&explaintext&exsectionformat=wiki&"
      L"redirects=1&format=json&"
      L"formatversion=2&titles=" +
      wtitle + extraQuery;

  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);

  if (response.find("\"missing\":true") != std::string::npos) {
    return "ERROR";
  }

  std::string key = "\"extract\":\"";
  size_t pos = response.find(key);
  if (pos != std::string::npos) {
    size_t start = pos + key.length();
    size_t end = std::string::npos;

    bool escaped = false;
    for (size_t i = start; i < response.length(); ++i) {
      if (escaped) {
        escaped = false;
      } else {
        if (response[i] == '\\') {
          escaped = true;
        } else if (response[i] == '"') {
          end = i;
          break;
        }
      }
    }

    if (end != std::string::npos) {
      std::string extract = response.substr(start, end - start);
      extract = wiki_json::DecodeUnicodeEscape(extract);
      extract = wiki_json::ReplaceAll(extract, "\\n", "\n");
      extract = wiki_json::ReplaceAll(extract, "\\t", "\t");
      extract = wiki_json::ReplaceAll(extract, "\\\"", "\"");
      extract = wiki_json::ReplaceAll(extract, "\\/", "/");

      size_t originalLength = extract.length();

      LOG_INFO("WikiClient", "Extract fetched: {} bytes (limit param {})",
               originalLength, lengthLimit);
      return extract;
    }
  }
  return "(Failed to fetch extract)";
}

std::string WikiClient::FetchPageHtml(const std::string &title) {
  // Robot policyが記事HTMLの取得先として勧める通常ページ（/wiki/記事名、クエリなし）を使う。
  // CDNのキャッシュが効くので、REST APIのHTMLより応答が安定して速い。
  // 返るのはメニューなどを含むページ全体で、本文の取り出しは CourseHtml が行う。
  // 転送ページは転送先の内容がそのまま返る。
  std::string pageTitle = title;
  std::replace(pageTitle.begin(), pageTitle.end(), ' ', '_');
  const std::wstring path = L"/wiki/" + core::ToWString(UrlEncode(pageTitle));
  return PerformGetRequest(L"ja.wikipedia.org", path, 8u * 1024 * 1024);
}

std::string WikiClient::FetchPageTableText(const std::string& title) {
  return ExtractTableText(FetchPageHtml(title));
}

std::string WikiClient::ExtractTableText(const std::string& html) {
  const auto tableBlocks = wiki_json::ExtractTableBlocks(html);
  std::string combined;
  for (const auto &block : tableBlocks) {
    combined += wiki_json::StripHtmlToPlainText(block);
    combined += '\n';
  }

  LOG_INFO("WikiClient", "Table text fetched: {} bytes from {} table(s)",
           combined.length(), tableBlocks.size());
  return combined;
}

std::vector<WikiImageInfo> WikiClient::FetchPageImages(const std::string &title,
                                                       int maxImages) {
  std::vector<WikiImageInfo> result;
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  // REST API: UIアイコン等を除いた実コンテンツ画像のみを、キャプション・
  // 所属節番号・解像度別URL付きで返す。
  std::wstring path = L"/api/rest_v1/page/media-list/" + wtitle;
  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  if (response.empty()) {
    return result;
  }

  size_t itemsPos = response.find("\"items\":");
  if (itemsPos == std::string::npos) {
    return result;
  }
  size_t bracketPos = response.find('[', itemsPos);
  if (bracketPos == std::string::npos) {
    return result;
  }
  size_t arrEnd = wiki_json::SkipValue(response, bracketPos);

  size_t pos = bracketPos + 1;
  while (pos < arrEnd && static_cast<int>(result.size()) < maxImages) {
    size_t objStart = response.find('{', pos);
    if (objStart == std::string::npos || objStart >= arrEnd) {
      break;
    }
    size_t objEnd =
        std::min(wiki_json::SkipValue(response, objStart), arrEnd);

    std::string type;
    size_t nextPos = 0;
    wiki_json::ExtractStringField(response, "\"type\":\"", objStart,
                                  objEnd, type, nextPos);

    if (type == "image") {
      WikiImageInfo info;
      wiki_json::ExtractStringField(response, "\"title\":\"", objStart,
                                    objEnd, info.fileTitle, nextPos);
      wiki_json::ExtractBoolField(response, "\"leadImage\":", objStart,
                                  objEnd, info.leadImage);
      wiki_json::ExtractIntField(response, "\"section_id\":", objStart,
                                 objEnd, info.sectionId);
      wiki_json::ExtractStringField(response, "\"text\":\"", objStart,
                                    objEnd, info.caption, nextPos);

      std::string src;
      if (wiki_json::ExtractStringField(response, "\"src\":\"", objStart,
                                        objEnd, src, nextPos)) {
        if (src.rfind("//", 0) == 0) {
          src = "https:" + src;
        }
        info.thumbUrl = src;
      }

      if (!info.thumbUrl.empty()) {
        result.push_back(std::move(info));
      }
    }

    pos = objEnd;
  }

  LOG_INFO("WikiClient", "Fetched {} images for {}", result.size(), title);

  FilterImagesByAllowedLicense(result);
  LOG_INFO("WikiClient", "{} images remain after license filtering", result.size());

  return result;
}

namespace {
/** @brief タイトル比較用にMediaWikiの空白/アンダースコア表記ゆれを吸収します。*/
std::string NormalizeWikiTitle(std::string title) {
  std::replace(title.begin(), title.end(), '_', ' ');
  return title;
}

/** @brief LicenseShortNameがCC0/パブリックドメイン相当かどうかを判定します。*/
bool IsAllowedImageLicense(std::string licenseShortName) {
  std::transform(licenseShortName.begin(), licenseShortName.end(),
                 licenseShortName.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return licenseShortName.find("cc0") != std::string::npos ||
         licenseShortName.find("public domain") != std::string::npos;
}
}  // namespace

void WikiClient::FilterImagesByAllowedLicense(std::vector<WikiImageInfo> &images) {
  if (images.empty()) {
    return;
  }

  // タイトル毎の許可判定。imageinfoが取得できなかった画像は不許可のまま扱う
  // （ライセンス不明な画像を安全側に倒して非表示にするため）。
  std::unordered_map<std::string, bool> allowedByTitle;

  // 1リクエストのタイトル数はAPIの上限（50件）とURL長の両方で区切る
  constexpr size_t kMaxTitlesPerRequest = 50;
  constexpr size_t kMaxTitlesParamLength = 6000;
  size_t nextImage = 0;
  while (nextImage < images.size()) {
    std::string titlesParam;
    size_t titleCount = 0;
    while (nextImage < images.size() && titleCount < kMaxTitlesPerRequest) {
      const std::string encoded = UrlEncode(images[nextImage].fileTitle);
      if (titleCount > 0 &&
          titlesParam.size() + 3 + encoded.size() > kMaxTitlesParamLength) {
        break;
      }
      if (titleCount > 0) {
        titlesParam += "%7C";  // "|" のURLエンコード形
      }
      titlesParam += encoded;
      ++titleCount;
      ++nextImage;
    }

    std::wstring path = L"/w/api.php?action=query&titles=" +
                         core::ToWString(titlesParam) +
                         L"&prop=imageinfo&iiprop=extmetadata"
                         L"&iiextmetadatafilter=LicenseShortName"
                         L"&format=json&formatversion=2";
    std::string response = PerformGetRequest(L"ja.wikipedia.org", path);

    size_t pagesPos = response.find("\"pages\":[");
    if (pagesPos != std::string::npos) {
      size_t arrStart = response.find('[', pagesPos);
      size_t arrEnd = wiki_json::SkipValue(response, arrStart);

      size_t pos = arrStart + 1;
      while (pos < arrEnd) {
        size_t objStart = response.find('{', pos);
        if (objStart == std::string::npos || objStart >= arrEnd) {
          break;
        }
        size_t objEnd = std::min(wiki_json::SkipValue(response, objStart), arrEnd);

        std::string pageTitle;
        size_t nextPos = 0;
        wiki_json::ExtractStringField(response, "\"title\":\"", objStart, objEnd,
                                      pageTitle, nextPos);

        std::string license;
        size_t extPos = response.find("\"extmetadata\":", objStart);
        if (extPos != std::string::npos && extPos < objEnd) {
          size_t extObjStart = response.find('{', extPos);
          if (extObjStart != std::string::npos && extObjStart < objEnd) {
            size_t extObjEnd =
                std::min(wiki_json::SkipValue(response, extObjStart), objEnd);
            size_t licKeyPos = response.find("\"LicenseShortName\":", extPos);
            if (licKeyPos != std::string::npos && licKeyPos < extObjEnd) {
              wiki_json::ExtractStringField(response, "\"value\":\"", licKeyPos,
                                            extObjEnd, license, nextPos);
            }
          }
        }

        if (!pageTitle.empty()) {
          allowedByTitle[NormalizeWikiTitle(pageTitle)] =
              IsAllowedImageLicense(license);
        }

        pos = objEnd;
      }
    }
  }

  images.erase(
      std::remove_if(images.begin(), images.end(),
                     [&](const WikiImageInfo &info) {
                       auto it = allowedByTitle.find(
                           NormalizeWikiTitle(info.fileTitle));
                       return it == allowedByTitle.end() || !it->second;
                     }),
      images.end());
}

std::vector<WikiSectionInfo> WikiClient::FetchPageSections(const std::string &title) {
  std::vector<WikiSectionInfo> result;
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  std::wstring path =
      L"/w/api.php?action=parse&prop=tocdata&format=json&formatversion=2&page=" +
      wtitle;
  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  if (response.empty()) {
    return result;
  }

  size_t secPos = response.find("\"sections\":");
  if (secPos == std::string::npos) {
    return result;
  }
  size_t bracketPos = response.find('[', secPos);
  if (bracketPos == std::string::npos) {
    return result;
  }
  size_t arrEnd = wiki_json::SkipValue(response, bracketPos);

  size_t pos = bracketPos + 1;
  while (pos < arrEnd) {
    size_t objStart = response.find('{', pos);
    if (objStart == std::string::npos || objStart >= arrEnd) {
      break;
    }
    size_t objEnd =
        std::min(wiki_json::SkipValue(response, objStart), arrEnd);

    WikiSectionInfo info;
    wiki_json::ExtractIntField(response, "\"hLevel\":", objStart, objEnd,
                               info.level);

    std::string indexStr;
    size_t nextPos = 0;
    if (wiki_json::ExtractStringField(response, "\"index\":\"", objStart,
                                      objEnd, indexStr, nextPos)) {
      info.index = std::atoi(indexStr.c_str());
    }

    std::string line;
    if (wiki_json::ExtractStringField(response, "\"line\":\"", objStart,
                                      objEnd, line, nextPos)) {
      info.heading = wiki_json::StripHtmlTags(line);
    }

    if (!info.heading.empty()) {
      result.push_back(std::move(info));
    }

    pos = objEnd;
  }

  LOG_INFO("WikiClient", "Fetched {} sections for {}", result.size(), title);
  return result;
}

std::string WikiClient::DownloadBinary(const std::string &url, size_t maxBytes) {
  std::string u = url;
  if (u.rfind("https://", 0) == 0) {
    u = u.substr(8);
  } else if (u.rfind("http://", 0) == 0) {
    u = u.substr(7);
  }

  size_t slashPos = u.find('/');
  if (slashPos == std::string::npos) {
    LOG_ERROR("WikiClient", "DownloadBinary: invalid URL");
    return "";
  }

  std::wstring host = core::ToWString(u.substr(0, slashPos));
  std::wstring path = core::ToWString(u.substr(slashPos));
  std::string data = PerformGetRequest(host, path, maxBytes);
  LOG_INFO("WikiClient", "Downloaded binary: {} bytes", data.size());
  return data;
}

} // namespace game::systems

