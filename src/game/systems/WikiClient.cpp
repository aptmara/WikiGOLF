#include "WikiClient.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace game::systems {

WikiClient::WikiClient() {
  // セッションの確立
  m_hSession =
      WinHttpOpen(L"WikiPinball/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!m_hSession) {
    LOG_ERROR("WikiClient", "Failed to open WinHttp session");
  }

  // 接続の確立 (ja.wikipedia.org)
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

  // レスポンスの読み取り
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
  // API:
  // /w/api.php?action=query&list=random&rnnamespace=0&rnlimit=1&format=json
  std::string response = PerformGetRequest(
      L"ja.wikipedia.org", L"/w/"
                           L"api.php?action=query&list=random&rnnamespace=0&"
                           L"rnlimit=1&format=json&formatversion=2");

  // 簡易JSONパース: "title":" を探す
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

// 簡易的な文字列置換
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
 * @brief JSONの\uXXXX形式UnicodeエスケープをUTF-8に変換
 * @param str 変換対象文字列
 * @return UTF-8デコード済み文字列
 *
 * - 入力: JSONレスポンス内の文字列（\uXXXX形式を含む可能性）
 * - 変更: \uXXXXを対応するUTF-8バイト列に置換
 * - 出力: UTF-8エンコードされた文字列
 */
std::string DecodeUnicodeEscape(const std::string &str) {
  std::string result;
  result.reserve(str.size());

  for (size_t i = 0; i < str.size(); ++i) {
    if (i + 5 < str.size() && str[i] == '\\' && str[i + 1] == 'u') {
      // \uXXXX形式を検出
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

        // サロゲートペア処理（絵文字など）
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
          // High surrogate - 次のlow surrogateを探す
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
                // サロゲートペアからコードポイント計算
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                            (lowSurrogate - 0xDC00);
                i += 6; // 追加のエスケープシーケンス分スキップ
              }
            }
          }
        }

        // UTF-8エンコード
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

        i += 5; // \uXXXX の6文字分スキップ（forのi++で+1される）
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

  std::string encodedTitle = UrlEncode(title);

  // string -> wstring
  std::wstring wtitle = core::ToWString(encodedTitle);

  // URL構築: prop=links で内部リンク取得
  std::wstring path = L"/w/api.php?action=query&titles=" + wtitle +
                      L"&prop=links&pllimit=" + std::to_wstring(limit) +
                      L"&plnamespace=0&redirects=1&format=json&formatversion=2";

  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  LOG_INFO("WikiClient", "FetchPageLinks response length: {}",
           response.length());

  size_t linksStart = response.find("\"links\":");
  if (linksStart == std::string::npos) {
    LOG_WARN("WikiClient", "No links found in response");
    return links;
  }

  size_t pos = linksStart;
  while ((pos = response.find("\"title\":\"", pos)) != std::string::npos) {
    size_t start = pos + 9;
    size_t end = response.find("\"", start);
    if (end == std::string::npos)
      break;

    std::string linkTitle = response.substr(start, end - start);
    // エスケープ解除（\uXXXX形式のUnicodeエスケープ含む）
    linkTitle = ReplaceAll(linkTitle, "\\\"", "\"");
    linkTitle = ReplaceAll(linkTitle, "\\/", "/");
    linkTitle = DecodeUnicodeEscape(linkTitle);

    if (linkTitle != title) {
      links.push_back({linkTitle, linkTitle});
    }
    pos = end;
  }

  LOG_INFO("WikiClient", "Found {} links", links.size());
  return links;
}

std::vector<std::string>
WikiClient::FetchPageCategories(const std::string &title) {
  std::vector<std::string> categories;
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  // prop=categories
  std::wstring path =
      L"/w/api.php?action=query&titles=" + wtitle +
      L"&prop=categories&cllimit=50&format=json&formatversion=2";

  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);

  // "categories": [...] を探す
  size_t catStart = response.find("\"categories\":");
  if (catStart == std::string::npos) {
    // カテゴリがない、またはエラー
    return categories;
  }

  // カテゴリ配列の終わりの括弧を探す（簡易パースの限界として、次のキーまでを範囲とする）
  size_t catEnd = response.find("]", catStart);
  if (catEnd == std::string::npos)
    catEnd = response.length();

  size_t pos = catStart;
  while ((pos = response.find("\"title\":\"", pos)) != std::string::npos) {
    if (pos > catEnd)
      break; // 範囲外

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
  return "日本"; // フォールバック
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

  std::string key = "\"extract\":\"";
  size_t pos = response.find(key);
  if (pos != std::string::npos) {
    size_t start = pos + key.length();
    size_t end = std::string::npos;

    // エスケープを考慮して終端の " を探す
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
      // Unicodeエスケープを先にデコード（\uXXXX形式）
      extract = DecodeUnicodeEscape(extract);
      extract = ReplaceAll(extract, "\\n", "\n");
      extract = ReplaceAll(extract, "\\t", "\t");
      extract = ReplaceAll(extract, "\\\"", "\"");
      extract = ReplaceAll(extract, "\\/", "/");

      LOG_INFO("WikiClient", "Extract fetched: {} chars", extract.length());
      return extract;
    }
  }
  return "(概要を取得できませんでした)";
}

} // namespace game::systems
