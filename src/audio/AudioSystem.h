#pragma once
/**
 * @file AudioSystem.h
 * @brief XAudio2を使用したオーディオ再生システム
 */

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <xaudio2.h>


namespace core {
struct GameContext;
}

namespace game::systems {

// XAudio2コールバック（再生終了検知用）
class VoiceCallback : public IXAudio2VoiceCallback {
public:
  void STDMETHODCALLTYPE OnStreamEnd() override { isFinished = true; }

  // 他は空実装
  void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
  void STDMETHODCALLTYPE OnBufferEnd(void *) override {}
  void STDMETHODCALLTYPE OnBufferStart(void *) override {}
  void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
  void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}

  bool isFinished = false;
};

class AudioSystem {
public:
  AudioSystem() = default;
  ~AudioSystem();

  bool Initialize();
  void Shutdown();

  // 毎フレーム呼び出し（終了したボイスのクリーンアップなど）
  void Update(core::GameContext &ctx);

  /// @brief 効果音を再生
  /// @param name ファイル名 (Assets/sounds/以下のパス)
  void PlaySE(core::GameContext &ctx, const std::string &name,
              float volume = 1.0f, float pitch = 0.0f);

  /// @brief BGMを再生（ループ）
  /// @param name ファイル名
  void PlayBGM(core::GameContext &ctx, const std::string &name,
               float volume = 0.6f);

  /// @brief BGM停止
  void StopBGM();

  /// @brief 全体音量設定
  void SetMasterVolume(float volume);

private:
  Microsoft::WRL::ComPtr<IXAudio2> m_xaudio2;
  IXAudio2MasteringVoice *m_masterVoice = nullptr;

  struct ActiveVoice {
    IXAudio2SourceVoice *voice;
    VoiceCallback callback;
    std::string debugName;
  };

  // SE用プール（再生中リスト）
  std::vector<std::unique_ptr<ActiveVoice>> m_activeSEs;

  // BGM用
  IXAudio2SourceVoice *m_bgmVoice = nullptr;
  std::string m_currentBgmName;
  VoiceCallback m_bgmCallback; // ループするのでEndは来ないが
};

} // namespace game::systems
