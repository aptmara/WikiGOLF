#include "StringUtils.h"
#include <windows.h>

namespace core {

std::wstring ToWString(const std::string &str) {
  if (str.empty())
    return L"";
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0],
                      size_needed);
  return wstrTo;
}

std::string ToString(const std::wstring &wstr) {
  if (wstr.empty())
    return "";
  int size_needed = WideCharToMultiByte(
      CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &strTo[0],
                      size_needed, NULL, NULL);
  return strTo;
}

} // namespace core
