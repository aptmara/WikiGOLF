#pragma once
/**
 * @file TitleSceneSupport.h
 * @brief タイトル画面で共有する小さな変換処理
*/

#include "../../core/GameContext.h"
#include "../components/WikiComponents.h"
#include <string>

namespace game::scenes::title_scene_detail {

inline constexpr float kIntroAudioVolume = 0.8f;
inline constexpr float kStartConnectionTimeoutSeconds = 8.0f;

/**
 * @brief 標準スタート用にWiki開始情報を初期化します。
 * @details 前回のカスタムコースやロード済みデータを破棄し、通常開始の抽選へ戻します。
*/
inline void ResetStandardStartData(core::GameContext &ctx) {
  game::components::WikiGlobalData data;
  ctx.world.SetGlobal(std::move(data));
  LOG_INFO("TitleScene", "Reset WikiGlobalData for standard random start");
}

/**
 * @brief URLのパーセントエンコードを復元します。
*/
inline std::string UrlDecode(const std::string &src) {
  std::string result;
  for (size_t index = 0; index < src.length(); ++index) {
    if (src[index] == '%' && index + 2 < src.length()) {
      int value = 0;
      if (sscanf_s(src.substr(index + 1, 2).c_str(), "%x", &value) == 1) {
        result += static_cast<char>(value);
        index += 2;
      } else {
        result += src[index];
      }
    } else if (src[index] == '+') {
      result += ' ';
    } else {
      result += src[index];
    }
  }
  return result;
}

/**
 * @brief WikiのURLまたは記事名から記事タイトルを抽出します。
*/
inline std::string ExtractWikiTitle(const std::string &input) {
  const std::string prefix = "wikipedia.org/wiki/";
  const size_t position = input.find(prefix);
  std::string title = input;
  if (position != std::string::npos) {
    title = input.substr(position + prefix.length());
    const size_t hashPosition = title.find('#');
    if (hashPosition != std::string::npos) {
      title = title.substr(0, hashPosition);
    }
    const size_t queryPosition = title.find('?');
    if (queryPosition != std::string::npos) {
      title = title.substr(0, queryPosition);
    }
    title = UrlDecode(title);
  }
  return title;
}

} // namespace game::scenes::title_scene_detail

