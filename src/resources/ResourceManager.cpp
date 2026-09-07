/**
 * @file ResourceManager.cpp
 * @brief 統合リソース管理クラスの実装
 */

#include "ResourceManager.h"
#include "ResourceManagerInternals.h"
#include "../core/Logger.h"
#include <chrono>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>
#include <windows.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace resources {

ResourceManager::ResourceManager(graphics::GraphicsDevice &device)
    : m_device(device),
      m_meshPool(graphics::Mesh{}) // ダミーフォールバック用
      ,
      m_shaderPool(graphics::Shader{}) // ダミーフォールバック用
      ,
      m_audioPool(audio::AudioClip{}) // ダミーフォールバック用
{}

// 音声機能実装


// 音声機能実装 (Media Foundation)

AudioHandle ResourceManager::LoadAudio(const std::string &path) {
  const auto startedAt = std::chrono::steady_clock::now();
  if (auto it = m_audioCache.find(path); it != m_audioCache.end()) {
    // LOG_DEBUG("Resource", "LoadAudio cache hit: {} ({} ms)", path,
    //           ElapsedMs(startedAt));
    return it->second;
  }

  // MF初期化 (スレッドセーフではないが、メインスレッドからの呼び出しを想定)
  static bool mfInitialized = false;
  if (!mfInitialized) {
    if (FAILED(MFStartup(MF_VERSION))) {
      LOG_ERROR("Resource", "MFStartup failed while loading {} ({} ms)", path,
                ElapsedMs(startedAt));
      return {};
    }
    mfInitialized = true;
  }

  // パス変換 (UTF-8 -> Wide)
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), NULL, 0);
  std::wstring wpath(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), &wpath[0],
                      size_needed);

  // Source Reader作成
  Microsoft::WRL::ComPtr<IMFSourceReader> pReader;
  HRESULT hr = MFCreateSourceReaderFromURL(
      wpath.c_str(), NULL, &pReader); // 属性NULLでデフォルト挙動
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to create SourceReader for: {} (hr={:x})",
              path, (uint32_t)hr);
    return {};
  }

  // PCMフォーマットを要求
  Microsoft::WRL::ComPtr<IMFMediaType> pPartialType;
  MFCreateMediaType(&pPartialType);
  pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

  hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL,
                                    pPartialType.Get());
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to set media type to PCM for: {}", path);
    return {};
  }

  // 変換後の完全なフォーマットを取得
  Microsoft::WRL::ComPtr<IMFMediaType> pUncompressedAudioType;
  hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                    &pUncompressedAudioType);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to get current media type");
    return {};
  }

  // WAVEFORMATEXへ変換
  WAVEFORMATEX *pWfx = NULL;
  UINT32 cbFormat = 0;
  hr = MFCreateWaveFormatExFromMFMediaType(pUncompressedAudioType.Get(), &pWfx,
                                           &cbFormat);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to convert to WAVEFORMATEX");
    return {};
  }

  audio::AudioClip clip = {};
  clip.format.resize(cbFormat);
  memcpy(clip.format.data(), pWfx, cbFormat);
  CoTaskMemFree(pWfx);

  // データ読み込み
  while (true) {
    DWORD flags = 0;
    Microsoft::WRL::ComPtr<IMFSample> pSample;
    hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL,
                             &flags, NULL, &pSample);

    if (FAILED(hr))
      break;
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
      break;
    if (pSample == nullptr)
      continue;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
    hr = pSample->ConvertToContiguousBuffer(&pBuffer);
    if (FAILED(hr))
      continue;

    BYTE *pAudioData = NULL;
    DWORD cbBuffer = 0;
    hr = pBuffer->Lock(&pAudioData, NULL, &cbBuffer);
    if (SUCCEEDED(hr)) {
      try {
        size_t currentSize = clip.buffer.size();
        if (currentSize == 0) {
           clip.buffer.reserve(48 * 1024 * 1024);
        }
        clip.buffer.resize(currentSize + cbBuffer);
        memcpy(clip.buffer.data() + currentSize, pAudioData, cbBuffer);
      } catch (const std::bad_alloc& e) {}
      pBuffer->Unlock();
    }
  }

  LOG_INFO("Resource", "Loaded Audio (MF): {} ({} bytes, {} ms)", path,
           clip.buffer.size(), ElapsedMs(startedAt));

  auto handle = m_audioPool.Add(std::move(clip));
  m_audioCache[path] = handle;
  return handle;
}

graphics::Mesh *ResourceManager::GetMesh(MeshHandle handle) {
  if (handle.index == 0 && handle.generation == 0)
    return nullptr;
  return m_meshPool.Get(handle);
}

graphics::Shader *ResourceManager::GetShader(ShaderHandle handle) {
  if (handle.index == 0 && handle.generation == 0)
    return nullptr;
  return m_shaderPool.Get(handle);
}

audio::AudioClip *ResourceManager::GetAudio(AudioHandle handle) {
  if (handle.index == 0 && handle.generation == 0)
    return nullptr;
  return m_audioPool.Get(handle);
}

} // namespace resources
