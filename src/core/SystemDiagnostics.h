#pragma once
/**
 * @file SystemDiagnostics.h
 * @brief 不具合報告用の診断情報（ゲームの版・OS・CPU・メモリ・GPU・画面・設定）をまとめる
 * @details 製品版・体験版で最初の警告・エラーが出たときに、ログの先頭へ一度だけ書く。
 *          一般配布するビルドで使うため、個人を特定できる情報は集めない
 *          （ユーザー名・PC名・IPアドレス・機器のシリアル番号・フォルダのパスは含めない）。
*/

#include <string>

namespace core {

/**
 * @brief 診断情報を複数行の文字列で返す
 * @note Logger から呼ばれるので、この中でログを出してはいけない
*/
std::string CollectSystemDiagnostics();

/**
 * @brief ゲーム側の情報（画質設定など）を診断情報に足す
 * @param name 見出し（例: "Settings"）。同じ名前で呼ぶと置き換える
 * @param text 内容。個人情報を入れないこと
*/
void SetDiagnosticsSection(const std::string &name, const std::string &text);

} // namespace core
