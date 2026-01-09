/**
 * @file AudioSystem.cpp
 * @brief XAudio2実装
 */

#include "AudioSystem.h"
#include "../core/GameContext.h"
#include "../core/Logger.h"
#include "../resources/ResourceManager.h"
#include "AudioClip.h"

// XAudio2ライブラリリンク
#pragma comment(lib, "xaudio2.lib")

namespace game::systems {

AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Initialize() {
  HRESULT hr;

  // COM初期化 (二重初期化はS_FALSEが返るだけで問題ない)
  hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    LOG_ERROR("Audio", "Failed to init COM: {:08X}", (uint32_t)hr);
    // すでに別モードで初期化されている場合は警告のみ出すのが一般的だが、ここではエラーにせず続行を試みる
  }

  hr = XAudio2Create(m_xaudio2.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to init XAudio2: {:08X}", (uint32_t)hr);
    return false;
  }

#ifdef _DEBUG
  XAUDIO2_DEBUG_CONFIGURATION debug = {};
  debug.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
  debug.BreakMask = XAUDIO2_LOG_ERRORS;
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
    if (v->voice) {
      v->voice->Stop();
      v->voice->DestroyVoice();
    }
  }
  m_activeSEs.clear();

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
      (*it)->voice->DestroyVoice();
      it = m_activeSEs.erase(it);
    } else {
      ++it;
    }
  }
}

// ヘルパー：ファイルパス探索
static std::string FindAudioPath(const std::string &filename) {
  const char *searchPaths[] = {"Assets/sounds/",       "sounds/",
                               "../sounds/",           "../../sounds/",
                               "../../Assets/sounds/", "src/resources/sounds/"};

  for (const char *prefix : searchPaths) {
    std::string path = std::string(prefix) + filename;
    std::ifstream f(path, std::ios::binary);
    if (f.good())
      return path;
  }
  return "sounds/" + filename; // デフォルト
}

void AudioSystem::PlaySE(core::GameContext &ctx, const std::string &name,
                         float volume, float pitch) {
  if (!m_xaudio2)
    return;

  std::string path = FindAudioPath(name);

  // リソースロード（キャッシュ有効）
  auto handle = ctx.resource.LoadAudio(path);
  auto *clip = ctx.resource.GetAudio(handle);

  if (!clip || clip->buffer.empty()) {
    // 頻繁に出る警告を避けるため、初回のみまたは間引くなどの制御を入れると良いが、
    // ここでは見つからない場合のみ警告
    static std::string lastMissingFile;
    if (lastMissingFile != name) {
      LOG_WARN("Audio", "SE not found: {} (searched as {})", name, path);
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

  activeVoice->debugName = name;
  activeVoice->voice->SetVolume(volume);
  activeVoice->voice->SetFrequencyRatio(std::pow(2.0f, pitch)); // ピッチ変換

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

void AudioSystem::PlayBGM(core::GameContext &ctx, const std::string &name,
                          float volume) {
  if (!m_xaudio2)
    return;

  if (m_currentBgmName == name && m_bgmVoice) {
    return; // 同じBGMなら何もしない
  }

  StopBGM();

  std::string path = FindAudioPath(name);
  auto handle = ctx.resource.LoadAudio(path);
  auto *clip = ctx.resource.GetAudio(handle);

  if (!clip || clip->buffer.empty()) {
    LOG_WARN("Audio", "BGM not found: {} (searched as {})", name, path);
    return;
  }

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
  buffer.LoopCount = XAUDIO2_LOOP_INFINITE; // 無限ループ

  m_bgmVoice->SetVolume(volume);
  LOG_DEBUG("Audio", "Submitting BGM buffer ({} bytes)", buffer.AudioBytes);
  hr = m_bgmVoice->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    LOG_ERROR("Audio", "Failed to submit BGM buffer: {:08X}", (uint32_t)hr);
    return;
  }
  LOG_DEBUG("Audio", "Starting BGM voice");
  m_bgmVoice->Start();

  m_currentBgmName = name;
  LOG_INFO("Audio", "Playing BGM: {}", name);
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
