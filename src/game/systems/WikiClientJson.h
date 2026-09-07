#pragma once

/**
 * @file WikiClientJson.h
 * @brief Wikipedia API応答のJSON/HTML整形規則を定義します。
*/

#include "WikiClient.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace game::systems::wiki_json {

/** @brief JSON文字列のエスケープをUTF-8へ復元します。*/
std::string DecodeString(std::string value);

/** @brief JSON文字列フィールドを指定範囲から取得します。*/
bool ExtractStringField(const std::string& json, const std::string& key,
                        size_t searchFrom, size_t searchLimit,
                        std::string& value, size_t& nextPos);

/** @brief JSON整数フィールドを指定範囲から取得します。*/
bool ExtractIntField(const std::string& json, const std::string& key,
                     size_t searchFrom, size_t searchLimit, int& value);

/** @brief JSON真偽値フィールドを指定範囲から取得します。*/
bool ExtractBoolField(const std::string& json, const std::string& key,
                      size_t searchFrom, size_t searchLimit, bool& value);

/** @brief JSON値の終端位置を取得します。*/
size_t SkipValue(const std::string& json, size_t pos);

/** @brief HTML断片からテーブルブロックを抽出します。*/
std::vector<std::string> ExtractTableBlocks(const std::string& html);

/** @brief HTML断片を表示用の平文へ変換します。*/
std::string StripHtmlToPlainText(const std::string& html);

/** @brief HTMLタグを取り除きます。*/
std::string StripHtmlTags(const std::string& html);

/** @brief 文字列中の全置換を行います。*/
std::string ReplaceAll(std::string value, const std::string& from,
                       const std::string& to);

/** @brief UnicodeエスケープをUTF-8へ変換します。*/
std::string DecodeUnicodeEscape(const std::string& value);

/** @brief query APIのリンク配列を抽出します。*/
void ParseQueryPageLinks(const std::string& response,
                         const std::string& sourceTitle, int effectiveLimit,
                         std::vector<game::WikiLink>& links,
                         std::unordered_set<std::string>& seen);

/** @brief parse APIのリンク配列を抽出します。*/
void ParseRenderedPageLinks(const std::string& response,
                            const std::string& sourceTitle,
                            int effectiveLimit,
                            std::vector<game::WikiLink>& links,
                            std::unordered_set<std::string>& seen);

} // namespace game::systems::wiki_json
