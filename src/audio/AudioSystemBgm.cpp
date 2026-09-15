/**
 * @file AudioSystem.cpp
 * @brief XAudio2実装
*/

#include "AudioSystem.h"
#include "AudioSystemInternals.h"
#include "../core/GameContext.h"
#include "../core/Logger.h"
#include "../resources/ResourceManager.h"
#include "AudioClip.h"
#include <algorithm>
#include <cmath>
#include <fstream>

// XAudio2ライブラリリンク
#pragma comment(lib, "xaudio2.lib")


namespace game::systems {

void AudioSystem::PlayBGM(core::GameContext &ctx, const std::string &name,
                          float volume) {
  if (!m_xaudio2)
    return;

  if (m_currentBgmName == name && m_bgmVoice)
    return;

  StopBGM();

  std::string path = audio_detail::FindAudioPath(name);
  auto handle = ctx.resource.LoadAudio(path);
  auto *clip = ctx.resource.GetAudio(handle);

  if (!clip || clip->buffer.empty()) {
    LOG_WARN("Audio", "BGM not found: {} (searched as {})", name, path);
    return;
  }
  if (clip->format.empty()) {
    LOG_ERROR("Audio", "BGM format is empty for: {}", name);
    return;
  }
  LOG_INFO("Audio", "BGM format size: {}", clip->format.size());

  HRESULT hr = m_xaudio2->CreateSourceVoice(
      &m_bgmVoice, reinterpret_cast<const WAVEFORMATEX *>(clip->format.data()),
      0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_bgmCallback);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to create BGM voice");
    return;
  }

  XAUDIO2_BUFFER buffer = {};
  buffer.pAudioData = clip->buffer.data();
  buffer.AudioBytes = static_cast<UINT32>(clip->buffer.size());
  buffer.Flags = XAUDIO2_END_OF_STREAM;
  buffer.LoopCount = XAUDIO2_LOOP_INFINITE;

  m_bgmBaseVolume = volume;
  m_bgmVoice->SetVolume(volume * m_bgmDuckLevel);
  hr = m_bgmVoice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to submit BGM buffer");
    return;
  }
  m_bgmVoice->Start();
  m_currentBgmName = name;
}

void AudioSystem::StopBGM() {
  if (m_bgmVoice) {
    m_bgmVoice->Stop();
    m_bgmVoice->DestroyVoice();
    m_bgmVoice = nullptr;
  }
  m_currentBgmName.clear();
}

void AudioSystem::PlayJingleWithBgmDuck(core::GameContext &ctx,
                                        const std::string &label,
                                        const std::string &path, float volume,
                                        float fadeOutSeconds,
                                        float fadeInSeconds) {
  if (!m_xaudio2 || label.empty()) {
    return;
  }

  PlayOneShotFile(ctx, label, path, volume);
  if (m_oneShotVoices.find(label) == m_oneShotVoices.end()) {
    return; // 再生できなかった場合はBGMを下げない
  }

  m_bgmDuckLabel = label;
  m_bgmDuckFadeOutSpeed = 1.0f / std::max(0.01f, fadeOutSeconds);
  m_bgmDuckFadeInSpeed = 1.0f / std::max(0.01f, fadeInSeconds);
}

void AudioSystem::UpdateBgmDuck(float dt) {
  const bool jinglePlaying =
      !m_bgmDuckLabel.empty() &&
      m_oneShotVoices.find(m_bgmDuckLabel) != m_oneShotVoices.end();

  if (jinglePlaying) {
    m_bgmDuckLevel = std::max(0.0f, m_bgmDuckLevel - m_bgmDuckFadeOutSpeed * dt);
  } else {
    m_bgmDuckLabel.clear();
    if (m_bgmDuckLevel >= 1.0f) {
      return;
    }
    m_bgmDuckLevel = std::min(1.0f, m_bgmDuckLevel + m_bgmDuckFadeInSpeed * dt);
  }

  if (m_bgmVoice) {
    m_bgmVoice->SetVolume(m_bgmBaseVolume * m_bgmDuckLevel);
  }
}

void AudioSystem::SetMasterVolume(float volume) {
  if (m_masterVoice) {
    m_masterVoice->SetVolume(volume);
  }
}

} // namespace game::systems

