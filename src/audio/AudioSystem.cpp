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
    audio_detail::StopAndDestroyVoice(v->voice);
  }
  m_activeSEs.clear();

  for (auto &pair : m_loopingSEs) {
    audio_detail::StopAndDestroyVoice(pair.second->voice);
  }
  m_loopingSEs.clear();

  for (auto &pair : m_oneShotVoices) {
    audio_detail::StopAndDestroyVoice(pair.second->voice);
  }
  m_oneShotVoices.clear();

  for (auto &v : m_fadingVoices) {
    audio_detail::StopAndDestroyVoice(v->voice);
  }
  m_fadingVoices.clear();

  if (m_landingVoice) {
    audio_detail::StopAndDestroyVoice(m_landingVoice->voice);
    m_landingVoice.reset();
  }

  if (m_masterVoice) {
    m_masterVoice->DestroyVoice();
    m_masterVoice = nullptr;
  }

  m_xaudio2.Reset();
}

void AudioSystem::Update(core::GameContext &ctx) {
  const float dt = std::clamp(ctx.dt, 0.0f, 0.1f);

  // 終了したSEボイスを削除
  auto it = m_activeSEs.begin();
  while (it != m_activeSEs.end()) {
    if ((*it)->callback.isFinished) {
      audio_detail::StopAndDestroyVoice((*it)->voice);
      it = m_activeSEs.erase(it);
    } else {
      ++it;
    }
  }

  auto oneShotIt = m_oneShotVoices.begin();
  while (oneShotIt != m_oneShotVoices.end()) {
    if (oneShotIt->second->callback.isFinished) {
      audio_detail::StopAndDestroyVoice(oneShotIt->second->voice);
      oneShotIt = m_oneShotVoices.erase(oneShotIt);
    } else {
      ++oneShotIt;
    }
  }

  // 着地音ボイスの更新
  if (m_landingVoice) {
    if (m_landingVoice->callback.isFinished) {
      audio_detail::StopAndDestroyVoice(m_landingVoice->voice);
      m_landingVoice.reset();
    } else {
      m_landingVoice->durationTimer += dt;
      if (!m_landingVoice->isStopping && m_landingVoice->maxDuration > 0.0f &&
          m_landingVoice->durationTimer >= m_landingVoice->maxDuration) {
        m_landingVoice->isStopping = true;
        m_landingVoice->fadeSpeed = m_landingVoice->currentVolume / 0.15f;
      }
      if (m_landingVoice->isStopping) {
        m_landingVoice->currentVolume = std::max(
            0.0f,
            m_landingVoice->currentVolume - m_landingVoice->fadeSpeed * dt);
        m_landingVoice->voice->SetVolume(m_landingVoice->currentVolume);
        if (m_landingVoice->currentVolume <= 0.001f) {
          audio_detail::StopAndDestroyVoice(m_landingVoice->voice);
          m_landingVoice.reset();
        }
      }
    }
  }

  // ループSEボイスの音量追従更新
  for (auto &pair : m_loopingSEs) {
    auto &voice = pair.second;
    if (voice && voice->voice) {
      if (voice->currentVolume < voice->targetVolume) {
        voice->currentVolume = std::min(
            voice->targetVolume, voice->currentVolume + voice->fadeSpeed * dt);
        voice->voice->SetVolume(voice->currentVolume);
      } else if (voice->currentVolume > voice->targetVolume) {
        voice->currentVolume = std::max(
            voice->targetVolume, voice->currentVolume - 8.0f * dt);
        voice->voice->SetVolume(voice->currentVolume);
      }
    }
  }

  // フェードアウト中ボイスの更新
  auto fadeIt = m_fadingVoices.begin();
  while (fadeIt != m_fadingVoices.end()) {
    auto &v = *fadeIt;
    if (!v->voice || v->callback.isFinished) {
      audio_detail::StopAndDestroyVoice(v->voice);
      fadeIt = m_fadingVoices.erase(fadeIt);
      continue;
    }
    v->currentVolume =
        std::max(0.0f, v->currentVolume - v->fadeSpeed * dt);
    v->voice->SetVolume(v->currentVolume);
    if (v->currentVolume <= 0.001f) {
      audio_detail::StopAndDestroyVoice(v->voice);
      fadeIt = m_fadingVoices.erase(fadeIt);
    } else {
      ++fadeIt;
    }
  }
}

} // namespace game::systems
