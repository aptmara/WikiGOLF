#pragma once
/**
 * @file ProfilerInternals.h
 * @brief Profiler実装で共有する計算処理
 */

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace core::profiler_detail {

inline constexpr double kBytesPerMb = 1024.0 * 1024.0;

inline double ToMilliseconds(const std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

inline double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double position = percentile * static_cast<double>(values.size() - 1);
  const size_t lower = static_cast<size_t>(std::floor(position));
  const size_t upper = static_cast<size_t>(std::ceil(position));
  if (lower == upper) {
    return values[lower];
  }
  const double fraction = position - static_cast<double>(lower);
  return values[lower] + (values[upper] - values[lower]) * fraction;
}

inline double Average(const std::vector<double> &values) {
  if (values.empty()) {
    return 0.0;
  }
  return std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
}

inline double FramesPerSecond(double frameMilliseconds) {
  if (frameMilliseconds <= 0.0) {
    return 0.0;
  }
  return 1000.0 / frameMilliseconds;
}

inline std::string CsvEscape(std::string_view value) {
  bool needsQuotes = false;
  std::string escaped;
  escaped.reserve(value.size() + 2);
  for (const char c : value) {
    if (c == '"') {
      escaped += "\"\"";
      needsQuotes = true;
    } else {
      escaped += c;
      needsQuotes = needsQuotes || c == ',' || c == '\n' || c == '\r';
    }
  }
  if (needsQuotes) {
    return std::string("\"") + escaped + "\"";
  }
  return escaped;
}

inline uint64_t FileTimeToUint64(const FILETIME &time) {
  ULARGE_INTEGER value{};
  value.LowPart = time.dwLowDateTime;
  value.HighPart = time.dwHighDateTime;
  return value.QuadPart;
}

inline std::string MakeSessionName() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  std::ostringstream stream;
  stream << "session_" << std::put_time(&local, "%Y%m%d_%H%M%S");
  return stream.str();
}

struct ScopeAggregate {
  std::vector<double> inclusive;
  std::vector<double> exclusive;
  uint64_t calls = 0;
};

} // namespace core::profiler_detail

