#pragma once
/**
 * @file AudioSystemInternals.h
 * @brief オーディオ実装で共有する内部処理
*/

#include <fstream>
#include <string>
#include <xaudio2.h>

namespace game::systems::audio_detail {

/**
 * @brief 音声ファイルを既定のアセットパスから探索します。
*/
inline std::string FindAudioPath(const std::string &filename) {
  const char *searchPaths[] = {"Assets/sounds/"};
  for (const char *prefix : searchPaths) {
    const std::string path = std::string(prefix) + filename;
    {
      std::ifstream file(path, std::ios::binary);
      if (file.good()) {
        return path;
      }
    }

    const std::string pathMp3 = path + ".mp3";
    {
      std::ifstream file(pathMp3, std::ios::binary);
      if (file.good()) {
        return pathMp3;
      }
    }
  }
  return "Assets/sounds/" + filename;
}

/**
 * @brief XAudio2ボイスを停止して破棄します。
*/
inline void StopAndDestroyVoice(IXAudio2SourceVoice *&voice) {
  if (!voice) {
    return;
  }
  voice->Stop();
  voice->DestroyVoice();
  voice = nullptr;
}

} // namespace game::systems::audio_detail

