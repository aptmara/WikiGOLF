#pragma once
/**
 * @file DebugAutoStart.h
 * @brief 計測用に、起動直後のシーン移動と自動終了を環境変数で指定する（デバッグツール専用）。
 * @details 同じ条件で何度も測り比べるためのもの。製品ビルドには含めない。
 *          - WIKIGOLF_DEBUG_START=golf  起動直後に標準のコースへ入る
 *          - WIKIGOLF_DEBUG_EXIT_AFTER=秒  起動からその秒数たったら終了する（計測結果を書き切る）
 */

#include "DebugSceneRules.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace game::debug {

/** @brief 環境変数から読んだ自動操作の指定 */
struct DebugAutoStartOptions {
  std::optional<DebugSceneTarget> startScene;
  float exitAfterSeconds = 0.0f; /**< 0 以下なら自動終了しない */
};

/** @brief 指定文字列から移動先シーンを決めます（大文字小文字は区別しない）。 */
inline std::optional<DebugSceneTarget> ParseDebugStartScene(std::string_view value) {
  std::string lower;
  lower.reserve(value.size());
  for (char c : value) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "golf" || lower == "game") {
    return DebugSceneTarget::Golf;
  }
  if (lower == "settings") {
    return DebugSceneTarget::Settings;
  }
  if (lower == "result") {
    return DebugSceneTarget::Result;
  }
  return std::nullopt;
}

/** @brief 秒数の文字列を読みます。読めない・負の値は 0（自動終了しない）。 */
inline float ParseDebugExitAfter(std::string_view value) {
  const std::string text(value);
  char *end = nullptr;
  const float seconds = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || !(seconds > 0.0f)) {
    return 0.0f;
  }
  return seconds;
}

/** @brief 環境変数から自動操作の指定を読みます。 */
inline DebugAutoStartOptions ReadDebugAutoStartOptions() {
  DebugAutoStartOptions options;
  char *value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, "WIKIGOLF_DEBUG_START") == 0 && value) {
    options.startScene = ParseDebugStartScene(value);
    std::free(value);
    value = nullptr;
  }
  if (_dupenv_s(&value, &length, "WIKIGOLF_DEBUG_EXIT_AFTER") == 0 && value) {
    options.exitAfterSeconds = ParseDebugExitAfter(value);
    std::free(value);
  }
  return options;
}

} // namespace game::debug
