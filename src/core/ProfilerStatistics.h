#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace core::profiler_detail {

struct BoundedStatistics {
  static constexpr size_t kSampleCapacity = 4096;

  uint64_t count = 0;
  double total = 0.0;
  double maximum = 0.0;
  uint64_t randomState = 0x9E3779B97F4A7C15ull;
  std::vector<double> samples;

  void Add(double value) {
    ++count;
    total += value;
    maximum = count == 1 ? value : std::max(maximum, value);
    if (samples.size() < kSampleCapacity) {
      samples.push_back(value);
      return;
    }

    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    const uint64_t selected = randomState % count;
    if (selected < kSampleCapacity) {
      samples[static_cast<size_t>(selected)] = value;
    }
  }

  double Average() const {
    return count == 0 ? 0.0 : total / static_cast<double>(count);
  }

  double Percentile(double percentile) const {
    if (samples.empty()) {
      return 0.0;
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const double position =
        percentile * static_cast<double>(sorted.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    if (lower == upper) {
      return sorted[lower];
    }
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
  }
};

} // namespace core::profiler_detail
