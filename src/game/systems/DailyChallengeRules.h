#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>

namespace game::systems {

inline std::uint32_t MakeDailyChallengeSeed(int year, int month, int day) {
  return static_cast<std::uint32_t>(year * 10000 + month * 100 + day);
}

inline std::uint32_t ParseDailyChallengeSeed(std::string_view timestamp) {
  if (timestamp.size() < 10 || timestamp[4] != '-' || timestamp[7] != '-') {
    return 0;
  }

  const auto digit = [&](std::size_t index) -> int {
    const char value = timestamp[index];
    if (value < '0' || value > '9') {
      return -1;
    }
    return value - '0';
  };

  const int yearThousands = digit(0);
  const int yearHundreds = digit(1);
  const int yearTens = digit(2);
  const int yearOnes = digit(3);
  const int monthTens = digit(5);
  const int monthOnes = digit(6);
  const int dayTens = digit(8);
  const int dayOnes = digit(9);
  if (yearThousands < 0 || yearHundreds < 0 || yearTens < 0 ||
      yearOnes < 0 || monthTens < 0 || monthOnes < 0 || dayTens < 0 ||
      dayOnes < 0) {
    return 0;
  }

  const int year = yearThousands * 1000 + yearHundreds * 100 +
                   yearTens * 10 + yearOnes;
  const int month = monthTens * 10 + monthOnes;
  const int day = dayTens * 10 + dayOnes;
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    return 0;
  }
  return MakeDailyChallengeSeed(year, month, day);
}

class DailyChallengeRandom {
public:
  explicit DailyChallengeRandom(std::uint32_t seed) : m_rng(seed) {}

  std::size_t NextIndex(std::size_t candidateCount) {
    std::uniform_int_distribution<std::size_t> distribution(
        0, candidateCount - 1);
    return distribution(m_rng);
  }

private:
  std::mt19937 m_rng;
};

} // namespace game::systems
