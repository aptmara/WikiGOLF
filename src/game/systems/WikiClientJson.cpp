/**
 * @file WikiClientJson.cpp
 * @brief Wikipedia API応答のJSON/HTML解析を実装します。
 */

#include "WikiClientJson.h"
#include "../../core/StringUtils.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <unordered_set>

namespace game::systems::wiki_json {

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
 * @brief JSONのUnicodeエスケープをUTF-8へ変換します。
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


/**
 * @brief JSON文字列値をUTF-8文字列へ復元します。 山内陽
 */
std::string DecodeString(std::string value) {
  value = ReplaceAll(value, "\\\"", "\"");
  value = ReplaceAll(value, "\\/", "/");
  value = ReplaceAll(value, "\\\\", "\\");
  value = DecodeUnicodeEscape(value);
  return value;
}

/**
 * @brief 指定キーのJSON文字列値を安全に切り出します。 山内陽
 */
bool ExtractStringField(const std::string &json, const std::string &key,
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
      value = DecodeString(json.substr(start, i - start));
      nextPos = i + 1;
      return true;
    }
  }

  return false;
}

/**
 * @brief JSONオブジェクト内の整数フィールドを取得します。 山内陽
 */
bool ExtractIntField(const std::string &json, const std::string &key,
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

  if (negative) {
    value = -parsed;
  } else {
    value = parsed;
  }
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
    if (!ExtractStringField(response, "\"title\":\"", pos, linksEnd,
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
    if (ExtractIntField(response, "\"ns\":", objectStart, objectEnd, ns) &&
        ns == 0 && response.find("\"exists\":true", objectStart) < objectEnd) {
      std::string linkTitle;
      size_t nextPos = 0;
      if (ExtractStringField(response, "\"*\":\"", objectStart, objectEnd,
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
      size_t digitStart = 2;
      size_t digitLength = entity.size() - 3;
      if (isHex) {
        digitStart = 3;
        digitLength = entity.size() - 4;
      }
      const std::string digits = entity.substr(digitStart, digitLength);
      try {
        int base = 10;
        if (isHex) {
          base = 16;
        }
        unsigned long codepoint = std::stoul(digits, nullptr, base);
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
    size_t nameStart = 0;
    if (!tag.empty() && tag[0] == '/') {
      nameStart = 1;
    }
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
      if (closeEnd == std::string::npos) {
        i = html.size();
      } else {
        i = closeEnd + 1;
      }
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

} // namespace game::systems::wiki_json

