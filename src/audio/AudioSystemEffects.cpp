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
#include <cmath>
#include <fstream>

// XAudio2ライブラリリンク
#pragma comment(lib, "xaudio2.lib")


namespace game::systems {

void AudioSystem::PlaySE(core::GameContext &ctx, const std::string &name,
                         float volume, float pitch) {
  if (!m_xaudio2)
    return;

  std::string path = audio_detail::FindAudioPath(name);
  resources::AudioHandle handle = {};
  audio::AudioClip *clip = nullptr;
  if (!path.empty()) {
    handle = ctx.resource.LoadAudio(path);
    clip = ctx.resource.GetAudio(handle);
  }

  if (!clip || clip->buffer.empty()) {
    static std::string lastMissingFile;
    if (lastMissingFile != name) {
      std::string searchSuffix;
      if (!path.empty()) {
        searchSuffix = std::format(" (searched as {})", path);
      }
      LOG_WARN("Audio", "SE not found: {}{}", name, searchSuffix);
      lastMissingFile = name;
    }
    return;
  }

  auto activeVoice = std::make_unique<ActiveVoice>();
  HRESULT hr = m_xaudio2->CreateSourceVoice(
      &activeVoice->voice,
      reinterpret_cast<const WAVEFORMATEX *>(clip->format.data()), 0,
      XAUDIO2_DEFAULT_FREQ_RATIO, &activeVoice->callback);

  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to create SourceVoice for SE: {}", name);
    return;
  }

  // バンカーSEの同時再生数制限（過大音量防止のため1つのみに制限）
  if (name.find("Bunker") != std::string::npos ||
      name.find("bunker") != std::string::npos) {
    for (size_t i = 0; i < m_activeSEs.size();) {
      if (m_activeSEs[i]->currentFile.find("Bunker") != std::string::npos ||
          m_activeSEs[i]->currentFile.find("bunker") != std::string::npos ||
          m_activeSEs[i]->debugName.find("Bunker") != std::string::npos ||
          m_activeSEs[i]->debugName.find("bunker") != std::string::npos) {
        audio_detail::StopAndDestroyVoice(m_activeSEs[i]->voice);
        m_activeSEs.erase(m_activeSEs.begin() + i);
      } else {
        ++i;
      }
    }
  }

  // 同時再生数管理（全体のキュー方式：古いものから破棄）
  if (m_activeSEs.size() >= MAX_ACTIVE_SE) {
    auto &oldest = m_activeSEs.front();
    audio_detail::StopAndDestroyVoice(oldest->voice);
    m_activeSEs.erase(m_activeSEs.begin());
  }

  activeVoice->debugName = name;
  activeVoice->voice->SetVolume(volume);
  activeVoice->voice->SetFrequencyRatio(std::pow(2.0f, pitch));

  XAUDIO2_BUFFER buffer = {};
  buffer.pAudioData = clip->buffer.data();
  buffer.AudioBytes = static_cast<UINT32>(clip->buffer.size());
  buffer.Flags = XAUDIO2_END_OF_STREAM;

  hr = activeVoice->voice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    activeVoice->voice->DestroyVoice();
    return;
  }

  activeVoice->voice->Start();
  m_activeSEs.push_back(std::move(activeVoice));
}

void AudioSystem::PlayOneShotFile(core::GameContext &ctx,
                                  const std::string &label,
                                  const std::string &path, float volume,
                                  float pitch) {
  if (!m_xaudio2 || label.empty() || path.empty()) {
    return;
  }

  StopOneShot(label);

  auto handle = ctx.resource.LoadAudio(path);
  auto *clip = ctx.resource.GetAudio(handle);
  if (!clip || clip->buffer.empty()) {
    LOG_WARN("Audio", "One-shot audio not found: {} ({})", label, path);
    return;
  }
  if (clip->format.empty()) {
    LOG_ERROR("Audio", "One-shot audio format is empty: {} ({})", label, path);
    return;
  }

  auto activeVoice = std::make_unique<ActiveVoice>();
  HRESULT hr = m_xaudio2->CreateSourceVoice(
      &activeVoice->voice,
      reinterpret_cast<const WAVEFORMATEX *>(clip->format.data()), 0,
      XAUDIO2_DEFAULT_FREQ_RATIO, &activeVoice->callback);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to create one-shot voice: {} ({})", label, path);
    return;
  }

  activeVoice->debugName = label;
  activeVoice->currentFile = path;
  activeVoice->voice->SetVolume(volume);
  activeVoice->voice->SetFrequencyRatio(std::pow(2.0f, pitch));

  XAUDIO2_BUFFER buffer = {};
  buffer.pAudioData = clip->buffer.data();
  buffer.AudioBytes = static_cast<UINT32>(clip->buffer.size());
  buffer.Flags = XAUDIO2_END_OF_STREAM;

  hr = activeVoice->voice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    audio_detail::StopAndDestroyVoice(activeVoice->voice);
    LOG_ERROR("Audio", "Failed to submit one-shot audio buffer: {} ({})", label,
              path);
    return;
  }

  hr = activeVoice->voice->Start();
  if (FAILED(hr)) {
    audio_detail::StopAndDestroyVoice(activeVoice->voice);
    LOG_ERROR("Audio", "Failed to start one-shot audio: {} ({})", label, path);
    return;
  }

  m_oneShotVoices[label] = std::move(activeVoice);
}

void AudioSystem::StopOneShot(const std::string &label) {
  auto it = m_oneShotVoices.find(label);
  if (it == m_oneShotVoices.end()) {
    return;
  }

  audio_detail::StopAndDestroyVoice(it->second->voice);
  m_oneShotVoices.erase(it);
}

void AudioSystem::SetLoopingSE(core::GameContext &ctx, const std::string &label,
                               const std::string &name, float volume,
                               float pitch) {
  if (!m_xaudio2)
    return;

  const bool shouldStop = (name.empty() || volume <= 0.001f);
  auto it = m_loopingSEs.find(label);

  if (shouldStop) {
    if (it != m_loopingSEs.end()) {
      auto voice = std::move(it->second);
      m_loopingSEs.erase(it);
      voice->isStopping = true;
      voice->fadeSpeed = std::max(0.1f, voice->currentVolume) / 0.08f;
      m_fadingVoices.push_back(std::move(voice));
    }
    return;
  }

  // 同一ファイル再生中ならボリューム・ピッチを更新
  if (it != m_loopingSEs.end() && it->second->currentFile == name) {
    it->second->targetVolume = volume;
    it->second->voice->SetFrequencyRatio(std::pow(2.0f, pitch));
    return;
  }

  // ファイル変更時：旧ボイスをフェードアウトへ移行
  if (it != m_loopingSEs.end()) {
    auto oldVoice = std::move(it->second);
    m_loopingSEs.erase(it);
    oldVoice->isStopping = true;
    oldVoice->fadeSpeed = std::max(0.1f, oldVoice->currentVolume) / 0.12f;
    m_fadingVoices.push_back(std::move(oldVoice));
  }

  std::string path = audio_detail::FindAudioPath(name);
  auto handle = ctx.resource.LoadAudio(path);
  auto *clip = ctx.resource.GetAudio(handle);

  if (!clip || clip->buffer.empty())
    return;

  auto activeVoice = std::make_unique<ActiveVoice>();
  HRESULT hr = m_xaudio2->CreateSourceVoice(
      &activeVoice->voice,
      reinterpret_cast<const WAVEFORMATEX *>(clip->format.data()), 0,
      XAUDIO2_DEFAULT_FREQ_RATIO, &activeVoice->callback);

  if (FAILED(hr))
    return;

  activeVoice->debugName = label;
  activeVoice->currentFile = name;
  activeVoice->currentVolume = 0.0f;
  activeVoice->targetVolume = volume;
  activeVoice->fadeSpeed = volume / 0.10f;
  activeVoice->voice->SetVolume(0.0f);
  activeVoice->voice->SetFrequencyRatio(std::pow(2.0f, pitch));

  XAUDIO2_BUFFER buffer = {};
  buffer.pAudioData = clip->buffer.data();
  buffer.AudioBytes = static_cast<UINT32>(clip->buffer.size());
  buffer.Flags = XAUDIO2_END_OF_STREAM;
  buffer.LoopCount = XAUDIO2_LOOP_INFINITE;

  hr = activeVoice->voice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    audio_detail::StopAndDestroyVoice(activeVoice->voice);
    return;
  }

  activeVoice->voice->Start();
  m_loopingSEs[label] = std::move(activeVoice);
}

void AudioSystem::PlayLandingSE(core::GameContext &ctx, const std::string &name,
                                float volume, float pitch, float maxDuration) {
  if (!m_xaudio2 || name.empty() || volume <= 0.001f)
    return;

  // 既存の着地音がある場合は速やかにフェードアウトへ移行
  if (m_landingVoice) {
    m_landingVoice->isStopping = true;
    m_landingVoice->fadeSpeed =
        std::max(0.1f, m_landingVoice->currentVolume) / 0.08f;
    m_fadingVoices.push_back(std::move(m_landingVoice));
    m_landingVoice.reset();
  }

  std::string path = audio_detail::FindAudioPath(name);
  auto handle = ctx.resource.LoadAudio(path);
  auto *clip = ctx.resource.GetAudio(handle);
  if (!clip || clip->buffer.empty())
    return;

  auto activeVoice = std::make_unique<ActiveVoice>();
  HRESULT hr = m_xaudio2->CreateSourceVoice(
      &activeVoice->voice,
      reinterpret_cast<const WAVEFORMATEX *>(clip->format.data()), 0,
      XAUDIO2_DEFAULT_FREQ_RATIO, &activeVoice->callback);
  if (FAILED(hr))
    return;

  activeVoice->debugName = "LandingSE";
  activeVoice->currentFile = name;
  activeVoice->currentVolume = volume;
  activeVoice->targetVolume = volume;
  activeVoice->fadeSpeed = 0.0f;
  activeVoice->durationTimer = 0.0f;
  activeVoice->maxDuration = maxDuration;
  activeVoice->isStopping = false;

  activeVoice->voice->SetVolume(volume);
  activeVoice->voice->SetFrequencyRatio(std::pow(2.0f, pitch));

  XAUDIO2_BUFFER buffer = {};
  buffer.pAudioData = clip->buffer.data();
  buffer.AudioBytes = static_cast<UINT32>(clip->buffer.size());
  buffer.Flags = XAUDIO2_END_OF_STREAM;

  hr = activeVoice->voice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    audio_detail::StopAndDestroyVoice(activeVoice->voice);
    return;
  }

  activeVoice->voice->Start();
  m_landingVoice = std::move(activeVoice);
}

void AudioSystem::StopLandingSE(float fadeSeconds) {
  if (!m_landingVoice)
    return;

  if (fadeSeconds <= 0.001f) {
    audio_detail::StopAndDestroyVoice(m_landingVoice->voice);
    m_landingVoice.reset();
  } else {
    m_landingVoice->isStopping = true;
    m_landingVoice->fadeSpeed =
        std::max(0.1f, m_landingVoice->currentVolume) / fadeSeconds;
    m_fadingVoices.push_back(std::move(m_landingVoice));
    m_landingVoice.reset();
  }
}

} // namespace game::systems

