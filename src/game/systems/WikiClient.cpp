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
  m_hSession =
      WinHttpOpen(L"WikiPinball/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!m_hSession) {
    LOG_ERROR("WikiClient", "Failed to open WinHttp session");
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

  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  // API負荷軽減: 1リクエストで最大500、最大3リクエスト（=1500リンク）
  // limit <= 0 の場合はデフォルト500に制限
  const int batchSize = 500;
  const int maxRequests = 3;
  const int effectiveLimit = (limit <= 0) ? 500 : limit;

  std::wstring plcontinue = L"";
  bool hasMore = true;
  int requestCount = 0;

  while (hasMore && requestCount < maxRequests) {
    std::wstring path =
        L"/w/api.php?action=query&titles=" + wtitle + L"&prop=links&pllimit=" +
        std::to_wstring(batchSize) +
        L"&plnamespace=0&redirects=1&format=json&formatversion=2";

    if (!plcontinue.empty()) {
      path += L"&plcontinue=" + plcontinue;
    }

    std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
    requestCount++;

    if (links.empty()) {
      LOG_INFO("WikiClient", "FetchPageLinks response length: {}",
               response.length());
    }

    size_t linksStart = response.find("\"links\":");
    if (linksStart != std::string::npos) {
      size_t pos = linksStart;
      while ((pos = response.find("\"title\":\"", pos)) != std::string::npos) {
        size_t start = pos + 9;
        size_t end = response.find("\"", start);
        if (end == std::string::npos)
          break;

        std::string linkTitle = response.substr(start, end - start);
        linkTitle = ReplaceAll(linkTitle, "\\\"", "\"");
        linkTitle = ReplaceAll(linkTitle, "\\/", "/");
        linkTitle = DecodeUnicodeEscape(linkTitle);

        if (linkTitle != title) {
          links.push_back({linkTitle, linkTitle});
        }
        pos = end;

        if ((int)links.size() >= effectiveLimit) {
          hasMore = false;
          break;
        }
      }
    }

    size_t contPos = response.find("\"plcontinue\":\"");
    if (contPos != std::string::npos && hasMore) {
      size_t contStart = contPos + 14;
      size_t contEnd = response.find("\"", contStart);
      if (contEnd != std::string::npos) {
        std::string continueValue =
            response.substr(contStart, contEnd - contStart);
        continueValue = ReplaceAll(continueValue, "\\|", "|");
        plcontinue = core::ToWString(UrlEncode(continueValue));
      } else {
        hasMore = false;
      }
    } else {
      hasMore = false;
    }
  }

  LOG_INFO("WikiClient", "Found {} links", links.size());
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
