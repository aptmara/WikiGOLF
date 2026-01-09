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

} // namespace core
