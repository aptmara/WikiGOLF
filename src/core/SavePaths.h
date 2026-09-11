#pragma once
/**
 * @file SavePaths.h
 * @brief ゲームのローカル保存領域（save/フォルダ）を扱うためのヘルパー
 * @details 現時点では実績データ (AchievementStore) のみがここを使う。
 *          将来的に settings.ini や save_playfab_profile.txt もここへ
 *          統合できるよう、保存先の決定ロジックだけを一箇所にまとめておく。
*/

#include <filesystem>
#include <string>

namespace core {

/** @brief ゲームのローカル保存領域のルートフォルダ名。*/
inline const std::filesystem::path &GetSaveDirectory() {
  static const std::filesystem::path kSaveDirectory = "save";
  return kSaveDirectory;
}

/**
 * @brief save/フォルダ配下のファイルパスを返します。存在しなければフォルダを作成します。
 * @param fileName save/ からの相対ファイル名（例: "achievements.txt"）
 * @return 呼び出し側でそのまま fstream に渡せるパス文字列
*/
inline std::string SaveFilePath(const std::string &fileName) {
  std::error_code errorCode;
  std::filesystem::create_directories(GetSaveDirectory(), errorCode);
  return (GetSaveDirectory() / fileName).string();
}

} // namespace core
