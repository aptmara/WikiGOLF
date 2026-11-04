/**
 * @file WikiClient.cpp
 * @brief WikiClient の実装
*/

#include "WikiClient.h"
#include "WikiClientJson.h"
#include "ContentFilter.h"
#include "DailyChallengeRules.h"
#include "WikiRequestPolicy.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
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
  // DEFAULT_PROXY はWindowsのプロキシ設定を参照せず、会社や学校のネットワークで
  // 接続できないことがあるため、システム設定に従う AUTOMATIC_PROXY を使う。
  m_hSession =
      WinHttpOpen(L"WikiGOLF/1.0 (https://github.com/aptmara/WikiGOLF)",
                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
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
                                          const std::wstring &path, size_t maxBytes) {
  HINTERNET hConnect = GetOrCreateConnection(server);
  if (!hConnect)
    return "";

  using Clock = std::chrono::steady_clock;
  const bool isWikiHost = server == L"ja.wikipedia.org";

  // WikiClientはスレッドごとに作られるため、同時接続の制限はプロセス全体で管理する。
  // 記事の取得と画像の取得は別系統で、それぞれ1件ずつ順に送る。
  struct HostGate {
    std::mutex mutex;
    Clock::time_point nextAllowed{};
  };
  static HostGate wikiGate;
  static HostGate mediaGate;
  HostGate &gate = isWikiHost ? wikiGate : mediaGate;
  std::lock_guard<std::mutex> lock(gate.mutex);

  const auto minInterval = std::chrono::milliseconds(
      isWikiHost ? wiki_request::kMinWikiIntervalMilliseconds : 0);
  const auto maxWait = std::chrono::seconds(wiki_request::kMaxRetryWaitSeconds);

  for (int attempt = 0; attempt < wiki_request::kMaxAttempts; ++attempt) {
    const auto waitTime = gate.nextAllowed - Clock::now();
    if (waitTime > maxWait) {
      LOG_WARN("WikiClient", "Skipped request while rate limited");
      return "";
    }
    if (waitTime > Clock::duration::zero()) {
      std::this_thread::sleep_for(waitTime);
    }
    gate.nextAllowed = Clock::now() + minInterval;

    int status = 0;
    std::wstring retryAfter;
    std::string response =
        SendGetRequest(hConnect, path, maxBytes, isWikiHost, status, retryAfter);
    if (wiki_request::IsSuccessStatus(status)) {
      return response;
    }
    if (!wiki_request::IsRetryableStatus(status)) {
      if (status != 0) {
        LOG_WARN("WikiClient", "Request failed with HTTP status {}", status);
      }
      return "";
    }

    const int delaySeconds = wiki_request::RetryDelaySeconds(retryAfter, attempt);
    gate.nextAllowed = Clock::now() + std::chrono::seconds(delaySeconds);
    LOG_WARN("WikiClient", "HTTP {} received, waiting {}s (attempt {})", status,
             delaySeconds, attempt + 1);
  }
  return "";
}

std::string WikiClient::SendGetRequest(HINTERNET hConnect,
                                       const std::wstring &path, size_t maxBytes,
                                       bool useCompression, int &status,
                                       std::wstring &retryAfter) {
  status = 0;
  retryAfter.clear();

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    LOG_ERROR("WikiClient", "Failed to open request");
    return "";
  }

  if (useCompression) {
    // gzipの付与と展開はWinHTTPが行う。失敗しても非圧縮で通信を続けられる。
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompression,
                     sizeof(decompression));
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

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (!WinHttpQueryHeaders(hRequest,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                           WINHTTP_NO_HEADER_INDEX)) {
    LOG_ERROR("WikiClient", "Failed to read response status");
    WinHttpCloseHandle(hRequest);
    return "";
  }
  status = static_cast<int>(statusCode);

  if (!wiki_request::IsSuccessStatus(status)) {
    wchar_t headerValue[64] = {};
    DWORD headerSize = sizeof(headerValue);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RETRY_AFTER,
                            WINHTTP_HEADER_NAME_BY_INDEX, headerValue,
                            &headerSize, WINHTTP_NO_HEADER_INDEX)) {
      retryAfter = headerValue;
    }
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

    if (maxBytes && (response.size() > maxBytes || dwSize > maxBytes-response.size())) {
      WinHttpCloseHandle(hRequest);
      return {};
    }
    std::vector<char> buffer(static_cast<size_t>(dwSize) + 1);
    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
      response.append(buffer.data(), dwDownloaded);
    } else {
      WinHttpCloseHandle(hRequest);
      return {};
    }
  } while (dwSize > 0);

  WinHttpCloseHandle(hRequest);
  return response;
}

std::string WikiClient::FetchRandomPageTitle() {
  // 除外対象を引いたら引き直す。全記事の数%程度なので、連続で当たることはまず無い。
  constexpr int kMaxAttempts = 10;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    m_lastServerDateSeed = 0;
    std::string response = PerformGetRequest(
        L"ja.wikipedia.org", L"/w/"
                             L"api.php?action=query&list=random&rnnamespace=0&"
                             L"rnlimit=1&curtimestamp=1&format=json&formatversion=2");

    constexpr std::string_view timestampField = "\"curtimestamp\":\"";
    const size_t timestampPosition = response.find(timestampField);
    if (timestampPosition != std::string::npos) {
      const size_t timestampStart = timestampPosition + timestampField.size();
      m_lastServerDateSeed = ParseDailyChallengeSeed(
          std::string_view(response).substr(timestampStart));
    }

    size_t titlePos = response.find("\"title\":\"");
    if (titlePos == std::string::npos) {
      return "Error";
    }
    size_t start = titlePos + 9;
    size_t end = response.find("\"", start);
    if (end == std::string::npos) {
      return "Error";
    }
    std::string title = response.substr(start, end - start);
    if (!ContentFilter::Instance().IsTitleBlocked(title)) {
      return title;
    }
  }
  return "日本";
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
  if (bracketPos == std::string::npos) {
    return result;
  }
  const size_t arrEnd = wiki_json::SkipValue(response, bracketPos);

  // 取得した候補のうち、除外対象ではない最初の記事を使う
  size_t pos = bracketPos + 1;
  while (pos < arrEnd) {
    const size_t objStart = response.find('{', pos);
    if (objStart == std::string::npos || objStart >= arrEnd) {
      break;
    }
    const size_t objEnd =
        std::min(wiki_json::SkipValue(response, objStart), arrEnd);
    pos = objEnd;

    std::string title;
    size_t nextPos = 0;
    if (!wiki_json::ExtractStringField(response, "\"title\":\"", objStart,
                                       objEnd, title, nextPos) ||
        ContentFilter::Instance().IsTitleBlocked(title)) {
      continue;
    }
    result.title = title;

    std::string thumbSrc;
    if (wiki_json::ExtractStringField(response, "\"source\":\"", objStart,
                                      objEnd, thumbSrc, nextPos)) {
      result.thumbnailUrl = thumbSrc;
    }
    break;
  }

  LOG_INFO("WikiClient", "FetchTargetPage: title='{}' hasThumbnail={}",
           result.title, !result.thumbnailUrl.empty());
  return result;
}

std::string WikiClient::FetchTargetPageTitle() { return FetchTargetPage().title; }

} // namespace game::systems
