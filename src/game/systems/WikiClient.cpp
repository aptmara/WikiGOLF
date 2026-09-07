/**
 * @file WikiClient.cpp
 * @brief WikiClient の実装
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
}

WikiClient::WikiClient() {
  // Wikimedia の User-Agent ポリシーに従い、連絡可能な識別情報を含める。
  // https://meta.wikimedia.org/wiki/User-Agent_policy
  m_hSession =
      WinHttpOpen(L"WikiGOLF/1.0 (https://github.com/aptmara/WikiGOLF)",
                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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
  for (auto &kv : m_hostConnections) {
    if (kv.second)
      WinHttpCloseHandle(kv.second);
  }
  if (m_hConnect)
    WinHttpCloseHandle(m_hConnect);
  if (m_hSession)
    WinHttpCloseHandle(m_hSession);
}

HINTERNET WikiClient::GetOrCreateConnection(const std::wstring &server) {
  if (server == L"ja.wikipedia.org") {
    return m_hConnect;
  }

  auto it = m_hostConnections.find(server);
  if (it != m_hostConnections.end()) {
    return it->second;
  }

  if (!m_hSession) {
    return nullptr;
  }

  HINTERNET conn = WinHttpConnect(m_hSession, server.c_str(),
                                  INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!conn) {
    LOG_ERROR("WikiClient", "Failed to connect to host");
    return nullptr;
  }

  m_hostConnections[server] = conn;
  return conn;
}

std::string WikiClient::PerformGetRequest(const std::wstring &server,
                                          const std::wstring &path) {
  HINTERNET hConnect = GetOrCreateConnection(server);
  if (!hConnect)
    return "";

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
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

WikiTargetPage WikiClient::FetchTargetPage(int thumbSize) {
  WikiTargetPage result;
  result.title = "Japan";

  // list=random ではなく generator=random にすることで、ランダム選定と
  // 代表サムネイル取得(prop=pageimages)を1リクエストにまとめる。
  std::wstring path =
      L"/w/api.php?action=query&generator=random&grnnamespace=0&grnlimit=5&"
      L"prop=pageimages&piprop=thumbnail&pithumbsize=" +
      std::to_wstring(thumbSize) + L"&format=json&formatversion=2";
  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  if (response.empty()) {
    return result;
  }

  size_t pagesPos = response.find("\"pages\":");
  if (pagesPos == std::string::npos) {
    return result;
  }
  size_t bracketPos = response.find('[', pagesPos);
  size_t objStart = std::string::npos;
  if (bracketPos != std::string::npos) {
    objStart = response.find('{', bracketPos);
  }
  if (bracketPos == std::string::npos || objStart == std::string::npos) {
    return result;
  }
  const size_t arrEnd = wiki_json::SkipValue(response, bracketPos);
  const size_t objEnd =
      std::min(wiki_json::SkipValue(response, objStart), arrEnd);

  std::string title;
  size_t nextPos = 0;
  if (wiki_json::ExtractStringField(response, "\"title\":\"", objStart,
                                    objEnd, title, nextPos)) {
    result.title = title;
  }

  std::string thumbSrc;
  if (wiki_json::ExtractStringField(response, "\"source\":\"", objStart,
                                    objEnd, thumbSrc, nextPos)) {
    result.thumbnailUrl = thumbSrc;
  }

  LOG_INFO("WikiClient", "FetchTargetPage: title='{}' hasThumbnail={}",
           result.title, !result.thumbnailUrl.empty());
  return result;
}

std::string WikiClient::FetchTargetPageTitle() { return FetchTargetPage().title; }

} // namespace game::systems
