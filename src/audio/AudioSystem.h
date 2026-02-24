#pragma once
/**
 * @file AudioSystem.h
 * @brief XAudio2を使用したオーディオ再生システム
 */

#include <map>
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

  /// @brief 同時再生最大数
  static constexpr size_t MAX_ACTIVE_SE = 16;

  /// @brief ループSEの設定（ラベルごとに状態管理）
  /// @param label 識別子（"BallRoll"等）
  /// @param name ファイル名。空文字、またはvolume=0で停止。
  void SetLoopingSE(core::GameContext &ctx, const std::string &label,
                    const std::string &name, float volume = 1.0f,
                    float pitch = 0.0f);

private:
  Microsoft::WRL::ComPtr<IXAudio2> m_xaudio2;
  IXAudio2MasteringVoice *m_masterVoice = nullptr;

  struct ActiveVoice {
    IXAudio2SourceVoice *voice;
    VoiceCallback callback;
    std::string debugName;
    std::string currentFile; // ループ用：現在流しているファイル名
  };

  // SE用プール（再生中リスト）
  std::vector<std::unique_ptr<ActiveVoice>> m_activeSEs;

  // ループSE（ラベル管理）
  std::map<std::string, std::unique_ptr<ActiveVoice>> m_loopingSEs;

  // BGM用
  IXAudio2SourceVoice *m_bgmVoice = nullptr;
  std::string m_currentBgmName;
  VoiceCallback m_bgmCallback; // ループするのでEndは来ないが
};

} // namespace game::systems
