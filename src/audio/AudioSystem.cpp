/**
 * @file AudioSystem.cpp
 * @brief XAudio2実装
 */

#include "AudioSystem.h"
#include "../core/GameContext.h"
#include "../core/Logger.h"
#include "../resources/ResourceManager.h"
#include "AudioClip.h"
#include <cmath>
#include <fstream>

// XAudio2ライブラリリンク
#pragma comment(lib, "xaudio2.lib")

namespace game::systems {

// ヘルパー：ファイルパス探索
static std::string FindAudioPath(const std::string &filename) {
  const char *searchPaths[] = {"Assets/sounds/"};

  for (const char *prefix : searchPaths) {
    std::string path = std::string(prefix) + filename;
    {
      std::ifstream f(path, std::ios::binary);
      if (f.good())
        return path;
    }
    // .mp3を試行
    std::string pathMp3 = path + ".mp3";
    {
      std::ifstream f(pathMp3, std::ios::binary);
      if (f.good())
        return pathMp3;
    }
  }
  return "Assets/sounds/" + filename; // デフォルト
}

static void StopAndDestroyVoice(IXAudio2SourceVoice *&voice) {
  if (!voice) {
    return;
  }
  voice->Stop();
  voice->DestroyVoice();
  voice = nullptr;
}

AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Initialize() {
  HRESULT hr;

  // COM初期化 (二重初期化はS_FALSEが返るだけで問題ない)
  hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    LOG_ERROR("Audio", "Failed to init COM: {:08X}", (uint32_t)hr);
  }

  hr = XAudio2Create(m_xaudio2.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to init XAudio2: {:08X}", (uint32_t)hr);
    return false;
  }

#ifdef _DEBUG
  XAUDIO2_DEBUG_CONFIGURATION debug = {};
  debug.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
  /// @brief 山内陽: Debug実行時にXAudio2の診断ブレークでプロセスが終了しないようにする。
  debug.BreakMask = 0;
  m_xaudio2->SetDebugConfiguration(&debug, 0);
#endif

  hr = m_xaudio2->CreateMasteringVoice(&m_masterVoice);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to create MasteringVoice: {:08X}", (uint32_t)hr);
    return false;
  }

  LOG_INFO("Audio", "AudioSystem Initialized.");
  return true;
}

void AudioSystem::Shutdown() {
  StopBGM();

  // SE全停止
  for (auto &v : m_activeSEs) {
    StopAndDestroyVoice(v->voice);
  }
  m_activeSEs.clear();

  for (auto &pair : m_loopingSEs) {
    StopAndDestroyVoice(pair.second->voice);
  }
  m_loopingSEs.clear();

  for (auto &pair : m_oneShotVoices) {
    StopAndDestroyVoice(pair.second->voice);
  }
  m_oneShotVoices.clear();

  if (m_masterVoice) {
    m_masterVoice->DestroyVoice();
    m_masterVoice = nullptr;
  }

  m_xaudio2.Reset();
}

void AudioSystem::Update(core::GameContext &ctx) {
  // 終了したSEボイスを削除
  auto it = m_activeSEs.begin();
  while (it != m_activeSEs.end()) {
    if ((*it)->callback.isFinished) {
      StopAndDestroyVoice((*it)->voice);
      it = m_activeSEs.erase(it);
    } else {
      ++it;
    }
  }

  auto oneShotIt = m_oneShotVoices.begin();
  while (oneShotIt != m_oneShotVoices.end()) {
    if (oneShotIt->second->callback.isFinished) {
      StopAndDestroyVoice(oneShotIt->second->voice);
      oneShotIt = m_oneShotVoices.erase(oneShotIt);
    } else {
      ++oneShotIt;
    }
  }
}

void AudioSystem::PlaySE(core::GameContext &ctx, const std::string &name,
                         float volume, float pitch) {
  if (!m_xaudio2)
    return;

  std::string path = FindAudioPath(name);
  resources::AudioHandle handle = {};
  audio::AudioClip *clip = nullptr;
  if (!path.empty()) {
    handle = ctx.resource.LoadAudio(path);
    clip = ctx.resource.GetAudio(handle);
  }

  if (!clip || clip->buffer.empty()) {
    static std::string lastMissingFile;
    if (lastMissingFile != name) {
      LOG_WARN("Audio", "SE not found: {}{}", name,
               path.empty() ? "" : std::format(" (searched as {})", path));
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

  // 特定のSEの同時再生数制限（例：サンド音は重複がうるさすぎないよう3つまで）
  if (name == "se_Bunker") {
    int count = 0;
    size_t oldestIdx = size_t(-1);
    for (size_t i = 0; i < m_activeSEs.size(); ++i) {
      if (m_activeSEs[i]->currentFile == name ||
          m_activeSEs[i]->debugName == name) {
        count++;
        if (oldestIdx == size_t(-1))
          oldestIdx = i;
      }
    }
    if (count >= 3 && oldestIdx != size_t(-1)) {
      StopAndDestroyVoice(m_activeSEs[oldestIdx]->voice);
      m_activeSEs.erase(m_activeSEs.begin() + oldestIdx);
    }
  }

  // 同時再生数管理（全体のキュー方式：古いものから破棄）
  if (m_activeSEs.size() >= MAX_ACTIVE_SE) {
    auto &oldest = m_activeSEs.front();
    StopAndDestroyVoice(oldest->voice);
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
    StopAndDestroyVoice(activeVoice->voice);
    LOG_ERROR("Audio", "Failed to submit one-shot audio buffer: {} ({})", label,
              path);
    return;
  }

  hr = activeVoice->voice->Start();
  if (FAILED(hr)) {
    StopAndDestroyVoice(activeVoice->voice);
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

  StopAndDestroyVoice(it->second->voice);
  m_oneShotVoices.erase(it);
}

void AudioSystem::SetLoopingSE(core::GameContext &ctx, const std::string &label,
                               const std::string &name, float volume,
                               float pitch) {
  if (!m_xaudio2)
    return;

  bool shouldStop = (name.empty() || volume <= 0.001f);
  auto it = m_loopingSEs.find(label);

  if (shouldStop) {
    if (it != m_loopingSEs.end()) {
      StopAndDestroyVoice(it->second->voice);
      m_loopingSEs.erase(it);
    }
    return;
  }

  if (it != m_loopingSEs.end() && it->second->currentFile == name) {
    it->second->voice->SetVolume(volume);
    it->second->voice->SetFrequencyRatio(std::pow(2.0f, pitch));
    return;
  }

  if (it != m_loopingSEs.end()) {
    StopAndDestroyVoice(it->second->voice);
    m_loopingSEs.erase(it);
  }

  std::string path = FindAudioPath(name);
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

  activeVoice->currentFile = name;
  activeVoice->voice->SetVolume(volume);
  activeVoice->voice->SetFrequencyRatio(std::pow(2.0f, pitch));

  XAUDIO2_BUFFER buffer = {};
  buffer.pAudioData = clip->buffer.data();
  buffer.AudioBytes = static_cast<UINT32>(clip->buffer.size());
  buffer.Flags = XAUDIO2_END_OF_STREAM;
  buffer.LoopCount = XAUDIO2_LOOP_INFINITE;

  hr = activeVoice->voice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    StopAndDestroyVoice(activeVoice->voice);
    return;
  }

  activeVoice->voice->Start();
  m_loopingSEs[label] = std::move(activeVoice);
}

void AudioSystem::PlayBGM(core::GameContext &ctx, const std::string &name,
                          float volume) {
  if (!m_xaudio2)
    return;

  if (m_currentBgmName == name && m_bgmVoice)
    return;

  StopBGM();

  std::string path = FindAudioPath(name);
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

  m_bgmVoice->SetVolume(volume);
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

void AudioSystem::SetMasterVolume(float volume) {
  if (m_masterVoice) {
    m_masterVoice->SetVolume(volume);
  }
}

} // namespace game::systems
