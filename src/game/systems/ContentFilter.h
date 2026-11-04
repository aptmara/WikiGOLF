#pragma once
/**
 * @file ContentFilter.h
 * @brief ゲームに出さない記事の判定（常時除外とセーフモード）
 *
 * 除外リストは docs/content-filter/build_filter_lists.py で生成し、
 * 起動時に一度だけ読み込む。読み込み後のリストは変更しないため、
 * 非同期ロードや経路探索のスレッドからも安全に参照できる。
*/

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game::systems {

class ContentFilter {
public:
  static constexpr const char *kDefaultPath = "Assets/data/content_filter.tsv";

  static ContentFilter &Instance() {
    static ContentFilter instance;
    return instance;
  }

  /**
   * @brief 「level<TAB>page_id<TAB>title」形式の除外リストを読み込みます。
   * @return 読み込んだ行数（不正な行は無視します）
  */
  std::size_t LoadFromStream(std::istream &input) {
    m_levelByTitle.clear();
    m_levelById.clear();
    std::size_t loaded = 0;
    std::string line;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const std::size_t firstTab = line.find('\t');
      const std::size_t secondTab =
          firstTab == std::string::npos ? std::string::npos
                                        : line.find('\t', firstTab + 1);
      if (secondTab == std::string::npos || secondTab + 1 >= line.size()) {
        continue;
      }
      int level = 0;
      int pageId = 0;
      if (!ParseInt(std::string_view(line).substr(0, firstTab), level) ||
          !ParseInt(std::string_view(line).substr(
                        firstTab + 1, secondTab - firstTab - 1),
                    pageId) ||
          (level != kAlwaysBlocked && level != kSafeModeBlocked)) {
        continue;
      }
      m_levelById[pageId] = static_cast<std::uint8_t>(level);
      m_levelByTitle[NormalizeTitle(line.substr(secondTab + 1))] =
          static_cast<std::uint8_t>(level);
      ++loaded;
    }
    return loaded;
  }

  /** @brief 除外リストを読み込みます。@return 1行以上読み込めたらtrue*/
  bool LoadFromFile(const std::string &path = kDefaultPath) {
    std::ifstream file(path);
    return file.is_open() && LoadFromStream(file) > 0;
  }

  void SetSafeMode(bool enabled) {
    m_safeMode.store(enabled, std::memory_order_relaxed);
  }
  bool IsSafeMode() const { return m_safeMode.load(std::memory_order_relaxed); }

  /** @brief 記事タイトル（リダイレクト名を含む）が除外対象かを返します。*/
  bool IsTitleBlocked(const std::string &title) const {
    if (m_levelByTitle.empty()) {
      return false;
    }
    const auto it = m_levelByTitle.find(NormalizeTitle(title));
    return it != m_levelByTitle.end() && IsLevelBlocked(it->second);
  }

  /** @brief 記事IDが除外対象かを返します（経路探索用）。*/
  bool IsPageIdBlocked(int pageId) const {
    if (m_levelById.empty()) {
      return false;
    }
    const auto it = m_levelById.find(pageId);
    return it != m_levelById.end() && IsLevelBlocked(it->second);
  }

  /** @brief 空白/アンダースコアとASCII大文字小文字の表記ゆれを吸収します。*/
  static std::string NormalizeTitle(std::string title) {
    std::transform(title.begin(), title.end(), title.begin(),
                   [](unsigned char character) -> char {
                     if (character == ' ') {
                       return '_';
                     }
                     if (character >= 'A' && character <= 'Z') {
                       return static_cast<char>(character - 'A' + 'a');
                     }
                     return static_cast<char>(character);
                   });
    return title;
  }

private:
  static constexpr int kAlwaysBlocked = 1;
  static constexpr int kSafeModeBlocked = 2;

  static bool ParseInt(std::string_view text, int &value) {
    const char *end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
  }

  bool IsLevelBlocked(std::uint8_t level) const {
    return level == kAlwaysBlocked || IsSafeMode();
  }

  std::unordered_map<std::string, std::uint8_t> m_levelByTitle;
  std::unordered_map<int, std::uint8_t> m_levelById;
  std::atomic<bool> m_safeMode{false};
};

} // namespace game::systems
