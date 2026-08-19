#include "WikiClient.h"
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

std::string ReplaceAll(std::string str, const std::string &from,
                       const std::string &to);
std::string DecodeUnicodeEscape(const std::string &str);

namespace {

constexpr int kWikiLinksBatchSize = 500;
constexpr int kUnlimitedWikiLinkLimit = std::numeric_limits<int>::max();

/**
 * @brief JSON文字列値をUTF-8文字列へ復元します。 山内陽
 */
std::string DecodeJsonString(std::string value) {
  value = ReplaceAll(value, "\\\"", "\"");
  value = ReplaceAll(value, "\\/", "/");
  value = ReplaceAll(value, "\\\\", "\\");
  value = DecodeUnicodeEscape(value);
  return value;
}

/**
 * @brief 指定キーのJSON文字列値を安全に切り出します。 山内陽
 */
bool ExtractJsonStringField(const std::string &json, const std::string &key,
                            size_t searchFrom, size_t searchLimit,
                            std::string &value, size_t &nextPos) {
  size_t keyPos = json.find(key, searchFrom);
  if (keyPos == std::string::npos || keyPos >= searchLimit) {
    return false;
  }

  const size_t start = keyPos + key.length();
  bool escaped = false;
  for (size_t i = start; i < json.length() && i < searchLimit; ++i) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (json[i] == '\\') {
      escaped = true;
      continue;
    }
    if (json[i] == '"') {
      value = DecodeJsonString(json.substr(start, i - start));
      nextPos = i + 1;
      return true;
    }
  }

  return false;
}

/**
 * @brief JSONオブジェクト内の整数フィールドを取得します。 山内陽
 */
bool ExtractJsonIntField(const std::string &json, const std::string &key,
                         size_t searchFrom, size_t searchLimit, int &value) {
  size_t keyPos = json.find(key, searchFrom);
  if (keyPos == std::string::npos || keyPos >= searchLimit) {
    return false;
  }

  size_t pos = keyPos + key.length();
  while (pos < json.length() && pos < searchLimit &&
         std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }

  bool negative = false;
  if (pos < json.length() && json[pos] == '-') {
    negative = true;
    ++pos;
  }

  if (pos >= json.length() ||
      !std::isdigit(static_cast<unsigned char>(json[pos]))) {
    return false;
  }

  int parsed = 0;
  while (pos < json.length() && pos < searchLimit &&
         std::isdigit(static_cast<unsigned char>(json[pos]))) {
    parsed = parsed * 10 + (json[pos] - '0');
    ++pos;
  }

  value = negative ? -parsed : parsed;
  return true;
}

/**
 * @brief ゲーム候補として扱う通常記事リンクを追加します。 山内陽
 */
void AddWikiLink(std::vector<game::WikiLink> &links,
                 std::unordered_set<std::string> &seen,
                 const std::string &sourceTitle,
                 const std::string &linkTitle) {
  if (linkTitle.empty() || linkTitle == sourceTitle ||
      seen.find(linkTitle) != seen.end()) {
    return;
  }

  links.push_back({linkTitle, linkTitle});
  seen.insert(linkTitle);
}

/**
 * @brief query APIのlinks配列から通常記事リンクを抽出します。 山内陽
 */
void ParseQueryPageLinks(const std::string &response,
                         const std::string &sourceTitle, int effectiveLimit,
                         std::vector<game::WikiLink> &links,
                         std::unordered_set<std::string> &seen) {
  size_t linksStart = response.find("\"links\":");
  if (linksStart == std::string::npos) {
    return;
  }

  size_t linksEnd = response.find("]", linksStart);
  if (linksEnd == std::string::npos) {
    linksEnd = response.length();
  }

  size_t pos = linksStart;
  while ((int)links.size() < effectiveLimit) {
    std::string linkTitle;
    size_t nextPos = 0;
    if (!ExtractJsonStringField(response, "\"title\":\"", pos, linksEnd,
                                linkTitle, nextPos)) {
      break;
    }

    AddWikiLink(links, seen, sourceTitle, linkTitle);
    pos = nextPos;
  }
}

/**
 * @brief parse APIのレンダリング後リンクから通常記事リンクを抽出します。 山内陽
 */
void ParseRenderedPageLinks(const std::string &response,
                            const std::string &sourceTitle, int effectiveLimit,
                            std::vector<game::WikiLink> &links,
                            std::unordered_set<std::string> &seen) {
  size_t linksStart = response.find("\"links\":[");
  if (linksStart == std::string::npos) {
    return;
  }

  size_t linksEnd = response.find("],", linksStart);
  if (linksEnd == std::string::npos) {
    linksEnd = response.find("]}", linksStart);
  }
  if (linksEnd == std::string::npos) {
    linksEnd = response.length();
  }

  size_t pos = linksStart;
  while ((int)links.size() < effectiveLimit) {
    size_t objectStart = response.find("{", pos);
    if (objectStart == std::string::npos || objectStart >= linksEnd) {
      break;
    }

    size_t objectEnd = response.find("}", objectStart);
    if (objectEnd == std::string::npos || objectEnd > linksEnd) {
      break;
    }

    int ns = -1;
    if (ExtractJsonIntField(response, "\"ns\":", objectStart, objectEnd, ns) &&
        ns == 0 && response.find("\"exists\":true", objectStart) < objectEnd) {
      std::string linkTitle;
      size_t nextPos = 0;
      if (ExtractJsonStringField(response, "\"*\":\"", objectStart, objectEnd,
                                 linkTitle, nextPos)) {
        AddWikiLink(links, seen, sourceTitle, linkTitle);
      }
    }

    pos = objectEnd + 1;
  }
}

} // namespace

WikiClient::WikiClient() {
  m_hSession =
      WinHttpOpen(L"WikiPinball/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!m_hSession) {
    LOG_ERROR("WikiClient", "Failed to open WinHttp session");
  }

  if (m_hSession) {
    // 名前解決/接続/送信/受信の各タイムアウトを明示的に短く設定する。
    // 既定値のままだとネットワークが不調な際に同期呼び出しが長時間ブロックし、
    // ウィンドウを閉じてからプロセスが終了するまでの時間を引き延ばしてしまう。
    WinHttpSetTimeouts(m_hSession, 5000, 5000, 5000, 10000);
  }

  if (m_hSession) {
    m_hConnect = WinHttpConnect(m_hSession, L"ja.wikipedia.org",
                                INTERNET_DEFAULT_HTTPS_PORT, 0);
  }
}

WikiClient::~WikiClient() {
  if (m_hConnect)
    WinHttpCloseHandle(m_hConnect);
  if (m_hSession)
    WinHttpCloseHandle(m_hSession);
}

std::string WikiClient::PerformGetRequest(const std::wstring &server,
                                          const std::wstring &path) {
  if (!m_hConnect)
    return "";

  HINTERNET hRequest = WinHttpOpenRequest(
      m_hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    LOG_ERROR("WikiClient", "Failed to open request");
    return "";
  }

  if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    LOG_ERROR("WikiClient", "Failed to send request");
    WinHttpCloseHandle(hRequest);
    return "";
  }

  if (!WinHttpReceiveResponse(hRequest, nullptr)) {
    LOG_ERROR("WikiClient", "Failed to receive response");
    WinHttpCloseHandle(hRequest);
    return "";
  }

  std::string response;
  DWORD dwSize = 0;
  DWORD dwDownloaded = 0;

  do {
    dwSize = 0;
    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
      break;
    }

    if (dwSize == 0)
      break;

    std::vector<char> buffer(dwSize + 1);
    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
      response.append(buffer.data(), dwDownloaded);
    }
  } while (dwSize > 0);

  WinHttpCloseHandle(hRequest);
  return response;
}

std::string WikiClient::FetchRandomPageTitle() {
  std::string response = PerformGetRequest(
      L"ja.wikipedia.org", L"/w/"
                           L"api.php?action=query&list=random&rnnamespace=0&"
                           L"rnlimit=1&format=json&formatversion=2");

  size_t titlePos = response.find("\"title\":\"");
  if (titlePos != std::string::npos) {
    size_t start = titlePos + 9;
    size_t end = response.find("\"", start);
    if (end != std::string::npos) {
      std::string title = response.substr(start, end - start);
      return title;
    }
  }
  return "Error";
}

std::string ReplaceAll(std::string str, const std::string &from,
                       const std::string &to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
  return str;
}

/**
 * @brief JSON Unicode escape to UTF-8 conversion
 */
std::string DecodeUnicodeEscape(const std::string &str) {
  std::string result;
  result.reserve(str.size());

  for (size_t i = 0; i < str.size(); ++i) {
    if (i + 5 < str.size() && str[i] == '\\' && str[i + 1] == 'u') {
      std::string hex = str.substr(i + 2, 4);
      bool validHex = true;
      for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
          validHex = false;
          break;
        }
      }

      if (validHex) {
        unsigned int codepoint = std::stoul(hex, nullptr, 16);

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
          if (i + 11 < str.size() && str[i + 6] == '\\' && str[i + 7] == 'u') {
            std::string lowHex = str.substr(i + 8, 4);
            bool validLow = true;
            for (char c : lowHex) {
              if (!std::isxdigit(static_cast<unsigned char>(c))) {
                validLow = false;
                break;
              }
            }
            if (validLow) {
              unsigned int lowSurrogate = std::stoul(lowHex, nullptr, 16);
              if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                            (lowSurrogate - 0xDC00);
                i += 6;
              }
            }
          }
        }

        if (codepoint <= 0x7F) {
          result += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FF) {
          result += static_cast<char>(0xC0 | (codepoint >> 6));
          result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
          result += static_cast<char>(0xE0 | (codepoint >> 12));
          result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
          result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0x10FFFF) {
          result += static_cast<char>(0xF0 | (codepoint >> 18));
          result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
          result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
          result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }

        i += 5;
        continue;
      }
    }
    result += str[i];
  }

  return result;
}

std::string WikiClient::UrlEncode(const std::string &str) {
  std::ostringstream encoded;
  for (unsigned char c : str) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded << c;
    } else {
      encoded << '%' << std::uppercase << std::hex << std::setw(2)
              << std::setfill('0') << (int)c;
    }
  }
  return encoded.str();
}

std::vector<game::WikiLink> WikiClient::FetchPageLinks(const std::string &title,
                                                       int limit) {
  std::vector<game::WikiLink> links;
  std::unordered_set<std::string> seen;

  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  // limit <= 0 は記事内リンクを取り切る。明示 limit は軽量モード用に尊重する。
  const int effectiveLimit = (limit <= 0) ? kUnlimitedWikiLinkLimit : limit;

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

    ParseQueryPageLinks(response, title, effectiveLimit, links, seen);

    size_t contPos = response.find("\"plcontinue\":\"");
    if (contPos != std::string::npos && (int)links.size() < effectiveLimit) {
      std::string continueValue;
      size_t nextPos = 0;
      if (ExtractJsonStringField(response, "\"plcontinue\":\"", contPos,
                                 response.length(), continueValue, nextPos)) {
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
    ParseRenderedPageLinks(response, title, effectiveLimit, links, seen);
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
    catTitle = DecodeUnicodeEscape(catTitle);

    categories.push_back(catTitle);
    pos = end;
  }

  LOG_INFO("WikiClient", "Fetched {} categories for {}", categories.size(),
           title);
  return categories;
}

std::string WikiClient::FetchTargetPageTitle() {
  std::string response = PerformGetRequest(
      L"ja.wikipedia.org",
      L"/w/api.php?action=query&list=random&rnnamespace=0&rnlimit=5&format="
      L"json&formatversion=2");

  size_t titlePos = response.find("\"title\":\"");
  if (titlePos != std::string::npos) {
    size_t start = titlePos + 9;
    size_t end = response.find("\"", start);
    if (end != std::string::npos) {
      std::string title = response.substr(start, end - start);
      title = ReplaceAll(title, "\\\"", "\"");
      title = ReplaceAll(title, "\\/", "/");
      return title;
    }
  }
  return "Japan";
}

std::string WikiClient::FetchPageExtract(const std::string &title,
                                         int lengthLimit) {
  std::string encodedTitle = UrlEncode(title);

  std::wstring wtitle = core::ToWString(encodedTitle);

  std::wstring path =
      L"/w/"
      L"api.php?action=query&prop=extracts&explaintext&redirects=1&format=json&"
      L"formatversion=2&titles=" +
      wtitle;

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
      extract = DecodeUnicodeEscape(extract);
      extract = ReplaceAll(extract, "\\n", "\n");
      extract = ReplaceAll(extract, "\\t", "\t");
      extract = ReplaceAll(extract, "\\\"", "\"");
      extract = ReplaceAll(extract, "\\/", "/");

      size_t originalLength = extract.length();

      LOG_INFO("WikiClient", "Extract fetched: {} bytes (limit param {})",
               originalLength, lengthLimit);
      return extract;
    }
  }
  return "(Failed to fetch extract)";
}

} // namespace game::systems
