#pragma once

#include <iomanip>
#include <sstream>
#include <string>

namespace game::systems {

inline bool IsValidPlayFabDisplayName(const std::wstring &name) {
  return name.size() >= 3 && name.size() <= 25 &&
         name.find_first_of(L"\r\n\t") == std::wstring::npos;
}

inline std::wstring FormatClearTime(int milliseconds) {
  if (milliseconds < 0) {
    milliseconds = 0;
  }
  const int totalSeconds = milliseconds / 1000;
  const int minutes = totalSeconds / 60;
  const int seconds = totalSeconds % 60;
  const int tenths = (milliseconds % 1000) / 100;
  std::wostringstream text;
  text << minutes << L":" << std::setw(2) << std::setfill(L'0') << seconds
       << L"." << tenths;
  return text.str();
}

} // namespace game::systems
