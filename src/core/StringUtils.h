#pragma once
/**
 * @file StringUtils.h
 * @brief 文字列操作ユーティリティ
 */

#include <string>

namespace core {

/**
 * @brief UTF-8文字列をUTF-16(wstring)に変換
 * @param str UTF-8文字列
 * @return UTF-16文字列
 */
std::wstring ToWString(const std::string &str);

/**
 * @brief UTF-16文字列をUTF-8(string)に変換
 * @param wstr UTF-16文字列
 * @return UTF-8文字列
 */
std::string ToString(const std::wstring &wstr);

/**
 * @brief UTF-8文字列を最大文字数でトリミング（コードポイント単位）
 * @param str UTF-8文字列
 * @param maxChars 最大文字数（0以下なら無制限）
 * @return トリミング済みUTF-8文字列
 */
std::string TrimUtf8ToLength(const std::string &str, size_t maxChars);

} // namespace core
