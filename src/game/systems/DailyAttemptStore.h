#pragma once
/**
 * @file DailyAttemptStore.h
 * @brief 最後にデイリーチャレンジを始めた日(日付seed)の保存
 * @details ランキングに載せるのは「その日の1回目」だけにするため、デイリーを
 *          始めた時点で日付seedを save/daily_attempt.txt へ書いておく。
*/

#include "../../core/SavePaths.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

namespace game::systems {

class DailyAttemptStore {
public:
  static constexpr const char *kFileName = "daily_attempt.txt";

  /** @brief 最後にデイリーを始めた日の日付seed(記録が無ければ0)を返します。*/
  static std::uint32_t LoadLastAttemptSeed() {
    std::ifstream input(core::SaveFilePath(kFileName));
    std::string line;
    while (std::getline(input, line)) {
      constexpr std::string_view kKey = "LAST_DAILY_SEED=";
      if (line.rfind(kKey.data(), 0) == 0) {
        return static_cast<std::uint32_t>(
            std::strtoul(line.c_str() + kKey.size(), nullptr, 10));
      }
    }
    return 0;
  }

  /** @brief デイリーを始めた日の日付seedを記録します。*/
  static bool SaveLastAttemptSeed(std::uint32_t seed) {
    std::ofstream output(core::SaveFilePath(kFileName), std::ios::trunc);
    if (!output) {
      return false;
    }
    output << "LAST_DAILY_SEED=" << seed << '\n';
    return static_cast<bool>(output);
  }
};

} // namespace game::systems
