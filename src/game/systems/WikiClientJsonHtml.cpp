/**
 * @file WikiClientJsonHtml.cpp
 * @brief Wikipedia応答のHTML/JSON終端処理です。
 */

#include "WikiClientJson.h"
#include <cctype>
#include <string>
#include <vector>

namespace game::systems::wiki_json {

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
        if (openEnd == std::string::npos) {
          cursor = html.size();
        } else {
          cursor = openEnd + 1;
        }
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
bool ExtractBoolField(const std::string &json, const std::string &key,
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
size_t SkipValue(const std::string &json, size_t pos) {
  if (pos >= json.size()) {
    return pos;
  }

  char c = json[pos];
  if (c == '{' || c == '[') {
    const char open = c;
    char close = ']';
    if (c == '{') {
      close = '}';
    }
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

} // namespace game::systems::wiki_json

