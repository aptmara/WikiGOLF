#pragma once
/**
 * @file AudioClip.h
 * @brief 音声データコンテナ
 */
#include <cstdint>
#include <vector>

namespace audio {

struct AudioClip {
  std::vector<uint8_t> buffer; ///< PCMデータ
  std::vector<uint8_t> format; ///< フォーマット情報 (WAVEFORMATEX)
  uint32_t loopStart = 0;      ///< ループ開始サンプル（0なら最初から）
  uint32_t loopLength = 0;     ///< ループ長さ（0なら最後まで）
};

} // namespace audio
