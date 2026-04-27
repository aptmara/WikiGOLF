#pragma once

#include "../../core/Logger.h"
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <vector>

namespace graphics {

class VideoPlayer {
public:
  VideoPlayer();
  ~VideoPlayer();

  VideoPlayer(const VideoPlayer&) = delete;
  VideoPlayer& operator=(const VideoPlayer&) = delete;

  /// @brief 動画ファイルの再生を初期化し、デコードスレッドを開始する
  bool Initialize(ID3D11Device* device, const std::string& filePath);

  /// @brief 毎フレーム呼び出し、必要に応じてテクスチャを更新する
  void Update(ID3D11DeviceContext* context, float dt);

  /// @brief 現在のフレームが書き込まれたテクスチャのSRVを取得する
  ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }

  /// @brief 再生が終了したか（キューも空になったか）
  bool IsFinished() const { return m_isFinished && m_frameQueue.empty(); }

  /// @brief 再生を強制終了する
  void Stop();

private:
  void DecodeThreadFunc();
  bool CreateTexture(int width, int height);

  Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
  Microsoft::WRL::ComPtr<IMFSourceReader> m_reader;
  ID3D11Device* m_device = nullptr;

  struct Frame {
    Microsoft::WRL::ComPtr<IMFSample> sample;
    LONGLONG timestamp; // 100ナノ秒単位のタイムスタンプ
  };

  std::queue<Frame> m_frameQueue;
  std::mutex m_mutex;
  std::thread m_decodeThread;
  std::atomic<bool> m_stopThread{false};
  std::atomic<bool> m_isFinished{false};

  LONGLONG m_currentPlaybackTime = 0; // 100ナノ秒単位での現在の再生位置
  int m_width = 0;
  int m_height = 0;
  LONG m_stride = 0;
  std::vector<uint8_t> m_frameUploadBuffer;
};

} // namespace graphics
