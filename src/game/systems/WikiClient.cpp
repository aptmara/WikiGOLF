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

/**
 * @brief Unicodeコードポイントを UTF-8 バイト列として追記します。 山内陽
 */
void AppendUtf8(std::string &out, unsigned long codepoint) {
  if (codepoint <= 0x7F) {
    out += static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    out += static_cast<char>(0xC0 | (codepoint >> 6));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (codepoint >> 12));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (codepoint >> 18));
    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

/**
 * @brief HTML実体参照（&amp; &nbsp; &#12345; &#xAB; 等）をデコードします。 山内陽
 */
std::string DecodeHtmlEntities(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] != '&') {
      out += text[i++];
      continue;
    }
    size_t semi = text.find(';', i);
    if (semi == std::string::npos || semi - i > 12) {
      out += text[i++];
      continue;
    }
    const std::string entity = text.substr(i, semi - i + 1);
    if (entity == "&amp;") { out += '&'; }
    else if (entity == "&lt;") { out += '<'; }
    else if (entity == "&gt;") { out += '>'; }
    else if (entity == "&quot;") { out += '"'; }
    else if (entity == "&apos;" || entity == "&#39;") { out += '\''; }
    else if (entity == "&nbsp;") { out += ' '; }
    else if (entity.size() > 3 && entity[1] == '#') {
      const bool isHex = (entity[2] == 'x' || entity[2] == 'X');
      const std::string digits =
          entity.substr(isHex ? 3 : 2, entity.size() - (isHex ? 4 : 3));
      try {
        unsigned long codepoint =
            std::stoul(digits, nullptr, isHex ? 16 : 10);
        AppendUtf8(out, codepoint);
      } catch (...) {
        out += entity; // 解釈できなければそのまま残す
      }
    } else {
      out += entity; // 未知の実体参照はそのまま残す
    }
    i = semi + 1;
  }
  return out;
}

/**
 * @brief HTML断片からタグを除去し、読める平文へ変換します。 山内陽
 * @details テーブル/インフォボックスの中身を articleText 相当のテキストへ
 *          変換するための簡易ストリッパー。<script>/<style> は中身ごと除去し、
 *          ブロック的なタグ（tr/table/p/div/li/br/見出し等）は改行、
 *          それ以外のタグは単語がくっつかないよう半角スペースへ変換する。
 */
std::string StripHtmlToPlainText(const std::string &html) {
  static const std::unordered_set<std::string> kBlockTags = {
      "tr", "table", "p",  "div", "li", "ul", "ol",
      "br", "h1", "h2", "h3", "h4", "h5", "h6", "dd", "dt", "caption"};

  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] != '<') {
      out += html[i++];
      continue;
    }

    const size_t tagEnd = html.find('>', i);
    if (tagEnd == std::string::npos) {
      break; // 末尾が壊れている場合はそこで打ち切る
    }

    const std::string tag = html.substr(i + 1, tagEnd - i - 1);
    size_t nameStart = (!tag.empty() && tag[0] == '/') ? 1 : 0;
    size_t nameEnd = nameStart;
    while (nameEnd < tag.size() &&
           !std::isspace(static_cast<unsigned char>(tag[nameEnd])) &&
           tag[nameEnd] != '/') {
      ++nameEnd;
    }
    std::string tagName = tag.substr(nameStart, nameEnd - nameStart);
    std::transform(tagName.begin(), tagName.end(), tagName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (tagName == "script" || tagName == "style") {
      const std::string closeTag = "</" + tagName;
      size_t closePos = html.find(closeTag, tagEnd + 1);
      if (closePos == std::string::npos) {
        break;
      }
      size_t closeEnd = html.find('>', closePos);
      i = (closeEnd == std::string::npos) ? html.size() : closeEnd + 1;
      continue;
    }

    if (kBlockTags.count(tagName)) {
      if (!out.empty() && out.back() != '\n') {
        out += '\n';
      }
    } else if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
      out += ' ';
    }
    i = tagEnd + 1;
  }

  return DecodeHtmlEntities(out);
}

/**
 * @brief HTML文字列から <table>...</table> ブロック（入れ子考慮）を抜き出します。 山内陽
 */
std::vector<std::string> ExtractTableBlocks(const std::string &html) {
  std::vector<std::string> blocks;
  size_t i = 0;
  while (i < html.size()) {
    size_t start = html.find("<table", i);
    if (start == std::string::npos) {
      break;
    }
    size_t cursor = html.find('>', start);
    if (cursor == std::string::npos) {
      break;
    }
    ++cursor;

    int depth = 1;
    while (depth > 0) {
      size_t nextOpen = html.find("<table", cursor);
      size_t nextClose = html.find("</table>", cursor);
      if (nextClose == std::string::npos) {
        depth = 0;
        cursor = html.size(); // 壊れている場合はそこまでを1ブロック扱いにする
        break;
      }
      if (nextOpen != std::string::npos && nextOpen < nextClose) {
        ++depth;
        size_t openEnd = html.find('>', nextOpen);
        cursor = (openEnd == std::string::npos) ? html.size() : openEnd + 1;
      } else {
        --depth;
        cursor = nextClose + 8; // strlen("</table>")
      }
    }
    blocks.push_back(html.substr(start, cursor - start));
    i = cursor;
  }
  return blocks;
}

/**
 * @brief JSONオブジェクト内の真偽値フィールドを取得します。
 */
bool ExtractJsonBoolField(const std::string &json, const std::string &key,
                          size_t searchFrom, size_t searchLimit,
                          bool &value) {
  size_t keyPos = json.find(key, searchFrom);
  if (keyPos == std::string::npos || keyPos >= searchLimit) {
    return false;
  }

  size_t pos = keyPos + key.length();
  while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }

  if (json.compare(pos, 4, "true") == 0) {
    value = true;
    return true;
  }
  if (json.compare(pos, 5, "false") == 0) {
    value = false;
    return true;
  }
  return false;
}

/**
 * @brief posが指すJSON値（文字列/オブジェクト/配列/その他リテラル）の終端の
 *        直後の位置を返します。ネストした{}/[]や文字列中の"も正しく無視します。
 */
size_t SkipJsonValue(const std::string &json, size_t pos) {
  if (pos >= json.size()) {
    return pos;
  }

  char c = json[pos];
  if (c == '{' || c == '[') {
    const char open = c;
    const char close = (c == '{') ? '}' : ']';
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = pos; i < json.size(); ++i) {
      char ch = json[i];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '"') {
          inString = false;
        }
        continue;
      }
      if (ch == '"') {
        inString = true;
      } else if (ch == open) {
        ++depth;
      } else if (ch == close) {
        --depth;
        if (depth == 0) {
          return i + 1;
        }
      }
    }
    return json.size();
  }

  if (c == '"') {
    bool escaped = false;
    for (size_t i = pos + 1; i < json.size(); ++i) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (json[i] == '\\') {
        escaped = true;
        continue;
      }
      if (json[i] == '"') {
        return i + 1;
      }
    }
    return json.size();
  }

  size_t i = pos;
  while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']') {
    ++i;
  }
  return i;
}

/**
 * @brief 簡易的にHTMLタグとよく使う実体参照を取り除きます（見出しテキスト整形用）。
 */
std::string StripHtmlTags(const std::string &html) {
  std::string result;
  result.reserve(html.size());
  bool inTag = false;
  for (char c : html) {
    if (c == '<') {
      inTag = true;
      continue;
    }
    if (c == '>') {
      inTag = false;
      continue;
    }
    if (!inTag) {
      result += c;
    }
  }
  result = ReplaceAll(result, "&amp;", "&");
  result = ReplaceAll(result, "&lt;", "<");
  result = ReplaceAll(result, "&gt;", ">");
  result = ReplaceAll(result, "&quot;", "\"");
  result = ReplaceAll(result, "&#39;", "'");
  return result;
}

} // namespace

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
  size_t objStart = (bracketPos != std::string::npos)
                        ? response.find('{', bracketPos)
                        : std::string::npos;
  if (bracketPos == std::string::npos || objStart == std::string::npos) {
    return result;
  }
  const size_t arrEnd = SkipJsonValue(response, bracketPos);
  const size_t objEnd = std::min(SkipJsonValue(response, objStart), arrEnd);

  std::string title;
  size_t nextPos = 0;
  if (ExtractJsonStringField(response, "\"title\":\"", objStart, objEnd, title,
                             nextPos)) {
    result.title = title;
  }

  std::string thumbSrc;
  if (ExtractJsonStringField(response, "\"source\":\"", objStart, objEnd,
                             thumbSrc, nextPos)) {
    result.thumbnailUrl = thumbSrc;
  }

  LOG_INFO("WikiClient", "FetchTargetPage: title='{}' hasThumbnail={}",
           result.title, !result.thumbnailUrl.empty());
  return result;
}

std::string WikiClient::FetchTargetPageTitle() { return FetchTargetPage().title; }

std::string WikiClient::FetchPageThumbnail(const std::string &title,
                                           int thumbSize) {
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  std::wstring path = L"/w/api.php?action=query&titles=" + wtitle +
                      L"&prop=pageimages&piprop=thumbnail&pithumbsize=" +
                      std::to_wstring(thumbSize) +
                      L"&format=json&formatversion=2";
  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  if (response.empty()) {
    return "";
  }

  std::string thumbSrc;
  size_t nextPos = 0;
  if (ExtractJsonStringField(response, "\"source\":\"", 0, response.size(),
                             thumbSrc, nextPos)) {
    return thumbSrc;
  }
  return "";
}

std::string WikiClient::FetchPageExtract(const std::string &title,
                                         int lengthLimit) {
  std::string encodedTitle = UrlEncode(title);

  std::wstring wtitle = core::ToWString(encodedTitle);

  // exsectionformat=wiki を指定することで、プレーンテキスト抽出でも
  // 見出し記法「== 見出し ==」が保持される（見出し解析・強調表示に利用）。
  std::wstring path =
      L"/w/"
      L"api.php?action=query&prop=extracts&explaintext&exsectionformat=wiki&"
      L"redirects=1&format=json&"
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

std::string WikiClient::FetchPageTableText(const std::string &title) {
  std::string encodedTitle = UrlEncode(title);
  std::wstring wtitle = core::ToWString(encodedTitle);

  std::wstring path = L"/w/api.php?action=parse&page=" + wtitle +
                      L"&prop=text&redirects=1&format=json&formatversion=2";

  std::string response = PerformGetRequest(L"ja.wikipedia.org", path);
  if (response.empty() ||
      response.find("\"missing\":true") != std::string::npos) {
    return "";
  }

  const std::string key = "\"text\":\"";
  size_t pos = response.find(key);
  if (pos == std::string::npos) {
    return "";
  }

  const size_t start = pos + key.length();
  size_t end = std::string::npos;
  bool escaped = false;
  for (size_t i = start; i < response.length(); ++i) {
    if (escaped) {
      escaped = false;
    } else if (response[i] == '\\') {
      escaped = true;
    } else if (response[i] == '"') {
      end = i;
      break;
    }
  }
  if (end == std::string::npos) {
    return "";
  }

  std::string html = response.substr(start, end - start);
  html = DecodeUnicodeEscape(html);
  html = ReplaceAll(html, "\\n", "\n");
  html = ReplaceAll(html, "\\t", "\t");
  html = ReplaceAll(html, "\\\"", "\"");
  html = ReplaceAll(html, "\\/", "/");

  const auto tableBlocks = ExtractTableBlocks(html);
  std::string combined;
  for (const auto &block : tableBlocks) {
    combined += StripHtmlToPlainText(block);
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
  size_t arrEnd = SkipJsonValue(response, bracketPos);

  size_t pos = bracketPos + 1;
  while (pos < arrEnd && static_cast<int>(result.size()) < maxImages) {
    size_t objStart = response.find('{', pos);
    if (objStart == std::string::npos || objStart >= arrEnd) {
      break;
    }
    size_t objEnd = std::min(SkipJsonValue(response, objStart), arrEnd);

    std::string type;
    size_t nextPos = 0;
    ExtractJsonStringField(response, "\"type\":\"", objStart, objEnd, type, nextPos);

    if (type == "image") {
      WikiImageInfo info;
      ExtractJsonStringField(response, "\"title\":\"", objStart, objEnd,
                             info.fileTitle, nextPos);
      ExtractJsonBoolField(response, "\"leadImage\":", objStart, objEnd,
                          info.leadImage);
      ExtractJsonIntField(response, "\"section_id\":", objStart, objEnd,
                         info.sectionId);
      ExtractJsonStringField(response, "\"text\":\"", objStart, objEnd,
                             info.caption, nextPos);

      std::string src;
      if (ExtractJsonStringField(response, "\"src\":\"", objStart, objEnd, src,
                                 nextPos)) {
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
  return result;
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
  size_t arrEnd = SkipJsonValue(response, bracketPos);

  size_t pos = bracketPos + 1;
  while (pos < arrEnd) {
    size_t objStart = response.find('{', pos);
    if (objStart == std::string::npos || objStart >= arrEnd) {
      break;
    }
    size_t objEnd = std::min(SkipJsonValue(response, objStart), arrEnd);

    WikiSectionInfo info;
    ExtractJsonIntField(response, "\"hLevel\":", objStart, objEnd, info.level);

    std::string indexStr;
    size_t nextPos = 0;
    if (ExtractJsonStringField(response, "\"index\":\"", objStart, objEnd,
                               indexStr, nextPos)) {
      info.index = std::atoi(indexStr.c_str());
    }

    std::string line;
    if (ExtractJsonStringField(response, "\"line\":\"", objStart, objEnd, line,
                               nextPos)) {
      info.heading = StripHtmlTags(line);
    }

    if (!info.heading.empty()) {
      result.push_back(std::move(info));
    }

    pos = objEnd;
  }

  LOG_INFO("WikiClient", "Fetched {} sections for {}", result.size(), title);
  return result;
}

std::string WikiClient::DownloadBinary(const std::string &url) {
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
  std::string data = PerformGetRequest(host, path);
  LOG_INFO("WikiClient", "Downloaded binary: {} bytes", data.size());
  return data;
}

} // namespace game::systems
