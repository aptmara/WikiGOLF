#pragma once
/**
 * @file LogPrivacy.h
 * @brief 一般配布するビルドのログから個人情報（ユーザー名・PC名・個人フォルダのパス）を伏せる
 * @details 製品版・体験版（WIKIGOLF_LOG_WARNINGS_ONLY）のログは、不具合報告で
 *          ユーザーから送ってもらうことを想定している。メッセージにはファイルパスや
 *          例外の説明がそのまま入るため、書き出す直前にここで置き換える。
*/

#include <string>
#include <vector>

namespace core {

/** @brief ログから伏せる文字列と、その代わりに書く文字列*/
struct PrivacyReplacement {
  std::string secret;      ///< 伏せる文字列（UTF-8）。英字の大小は区別しない
  std::string placeholder; ///< 代わりに書く文字列（例: "<USER>"）
  bool wholeWord = false;  ///< true なら英数字の途中にあるものは置き換えない（短い名前の誤爆防止）
};

/**
 * @brief 実行中の環境から伏せる対象を作る
 * @param gameDirectoryUtf8 ゲームを置いたフォルダ（親フォルダ名に個人名が入りうる）。空なら使わない
 * @details 対象: ゲームのフォルダ、ユーザーフォルダ（%USERPROFILE%）、OneDrive、
 *          ユーザー名、PC名、ドメイン名。長いものから順に置き換わるよう並べて返す。
*/
std::vector<PrivacyReplacement>
BuildPrivacyReplacementsFromEnvironment(const std::string &gameDirectoryUtf8);

/**
 * @brief 文字列から個人情報を伏せる
 * @details replacements を順に置き換えたあと、最後に "\\Users\\<名前>\\" 形式の
 *          パスに残った名前も "<USER>" にする（環境変数に無い別ユーザーのパス対策）。
 *          "\\" と "/" のどちらの区切りでも一致する。
*/
std::string RedactPrivateText(std::string text,
                              const std::vector<PrivacyReplacement> &replacements);

} // namespace core
