#include "src/core/ProfilerStatistics.h"

#include <cmath>
#include <iostream>

int main() {
  core::profiler_detail::BoundedStatistics stats;
  for (int value = 1; value <= 10000; ++value) {
    stats.Add(static_cast<double>(value));
  }

  if (stats.count != 10000 || std::abs(stats.Average() - 5000.5) > 0.001 ||
      stats.maximum != 10000.0 ||
      stats.samples.size() != stats.kSampleCapacity) {
    std::cerr << "Profiler bounded statistics aggregate failed\n";
    return 1;
  }

  const double p95 = stats.Percentile(0.95);
  if (p95 < 9000.0 || p95 > 9900.0) {
    std::cerr << "Profiler bounded statistics percentile failed: " << p95
              << '\n';
    return 1;
  }
  return 0;
}
