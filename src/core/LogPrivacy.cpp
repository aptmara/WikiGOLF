/**
 * @file LogPrivacy.cpp
 * @brief ログから個人情報を伏せる処理の実装
*/

#include "LogPrivacy.h"

#include <algorithm>
#include <cstddef>
#include <windows.h>

namespace core {
namespace {

char AsciiLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool IsAsciiWordChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsSeparator(char c) { return c == '\\' || c == '/'; }

bool IsAscii(const std::string &text) {
  return std::all_of(text.begin(), text.end(), [](char c) {
    return static_cast<unsigned char>(c) < 0x80;
  });
}

// 英字の大小と区切り文字（\ と /）の違いを無視して比べる
bool MatchesAt(const std::string &text, std::size_t pos,
               const std::string &secret) {
  if (pos + secret.size() > text.size()) return false;
  for (std::size_t i = 0; i < secret.size(); ++i) {
    const char a = text[pos + i];
    const char b = secret[i];
    if (IsSeparator(a) && IsSeparator(b)) continue;
    if (AsciiLower(a) != AsciiLower(b)) return false;
  }
  return true;
}

bool MatchesReplacementAt(const std::string &text, std::size_t pos,
                          const PrivacyReplacement &replacement) {
  const std::string &secret = replacement.secret;
  if (secret.empty() || !MatchesAt(text, pos, secret)) return false;
  // 英字だけの名前は、単語の途中（例: "data" の中の "at"）を置き換えない
  if (replacement.wholeWord && IsAscii(secret)) {
    const std::size_t end = pos + secret.size();
    if ((pos > 0 && IsAsciiWordChar(text[pos - 1])) ||
        (end < text.size() && IsAsciiWordChar(text[end]))) {
      return false;
    }
  }
  return true;
}

// 1 回の走査で置き換える。置き換え後の文字（"<USER>" など）を再び書き換えないため
std::string ReplaceAll(const std::string &text,
                       const std::vector<PrivacyReplacement> &replacements) {
  std::string result;
  result.reserve(text.size());
  std::size_t pos = 0;
  while (pos < text.size()) {
    const PrivacyReplacement *matched = nullptr;
    for (const PrivacyReplacement &replacement : replacements) {
      if (MatchesReplacementAt(text, pos, replacement)) {
        matched = &replacement;
        break;
      }
    }
    if (matched) {
      result += matched->placeholder;
      pos += matched->secret.size();
    } else {
      result += text[pos];
      ++pos;
    }
  }
  return result;
}

// "\Users\<名前>\" の名前部分を伏せる（環境変数に無いユーザーのパスも残さない）
void RedactUsersDirectoryNames(std::string &text) {
  static const std::string kUsers = "\\users\\";
  static const std::string kPlaceholder = "<USER>";
  std::size_t pos = 0;
  while (pos + kUsers.size() <= text.size()) {
    if (!MatchesAt(text, pos, kUsers)) {
      ++pos;
      continue;
    }
    const std::size_t nameBegin = pos + kUsers.size();
    std::size_t nameEnd = nameBegin;
    while (nameEnd < text.size() && !IsSeparator(text[nameEnd]) &&
           text[nameEnd] != '\'' && text[nameEnd] != '"' &&
           text[nameEnd] != '\n' && text[nameEnd] != '\r') {
      ++nameEnd;
    }
    const std::string name = text.substr(nameBegin, nameEnd - nameBegin);
    // 共有フォルダ（Public など）と、すでに伏せたものはそのまま残す
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), AsciiLower);
    const bool keep = name.empty() || lowered == "public" ||
                      lowered == "default" || lowered == "all users" ||
                      name == kPlaceholder;
    if (!keep) {
      text.replace(nameBegin, name.size(), kPlaceholder);
      nameEnd = nameBegin + kPlaceholder.size();
    }
    pos = nameEnd;
  }
}

std::string WideToUtf8(const std::wstring &wide) {
  if (wide.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                       static_cast<int>(wide.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string utf8(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::string GetEnvironmentUtf8(const wchar_t *name) {
  const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) return {};
  std::wstring value(size, L'\0');
  const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
  value.resize(written);
  return WideToUtf8(value);
}

std::string TrimTrailingSeparators(std::string path) {
  while (path.size() > 3 && IsSeparator(path.back())) path.pop_back();
  return path;
}

} // namespace

std::vector<PrivacyReplacement>
BuildPrivacyReplacementsFromEnvironment(const std::string &gameDirectoryUtf8) {
  std::vector<PrivacyReplacement> replacements;
  auto addPath = [&](std::string path, const char *placeholder) {
    path = TrimTrailingSeparators(std::move(path));
    // ドライブ直下（"C:\" など）を伏せると読めなくなるので対象にしない
    if (path.size() > 3) replacements.push_back({path, placeholder, false});
  };
  addPath(gameDirectoryUtf8, "<GAME_DIR>");
  addPath(GetEnvironmentUtf8(L"OneDrive"), "%OneDrive%");
  addPath(GetEnvironmentUtf8(L"USERPROFILE"), "%USERPROFILE%");

  auto addName = [&](const std::string &name, const char *placeholder) {
    // 1 文字の名前は誤爆が多すぎるので伏せない（パスの中なら最後の処理で伏せる）
    if (name.size() >= 2) replacements.push_back({name, placeholder, true});
  };
  const std::string userName = GetEnvironmentUtf8(L"USERNAME");
  const std::string computerName = GetEnvironmentUtf8(L"COMPUTERNAME");
  const std::string domainName = GetEnvironmentUtf8(L"USERDOMAIN");
  addName(userName, "<USER>");
  addName(computerName, "<PC>");
  if (domainName != computerName) addName(domainName, "<DOMAIN>");

  // ゲームのフォルダがユーザーフォルダの中にあるときなど、長いものから置き換える
  std::stable_sort(replacements.begin(), replacements.end(),
                   [](const PrivacyReplacement &a, const PrivacyReplacement &b) {
                     return a.secret.size() > b.secret.size();
                   });
  return replacements;
}

std::string RedactPrivateText(std::string text,
                              const std::vector<PrivacyReplacement> &replacements) {
  // 長いものを先に試す（ゲームのフォルダがユーザーフォルダの中にある場合など）
  std::vector<PrivacyReplacement> ordered = replacements;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const PrivacyReplacement &a, const PrivacyReplacement &b) {
                     return a.secret.size() > b.secret.size();
                   });
  text = ReplaceAll(text, ordered);
  RedactUsersDirectoryNames(text);
  return text;
}

} // namespace core
