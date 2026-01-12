#include "ResourceManager.h"
#include "../core/Logger.h"
#include "../graphics/FbxLoader.h"
#include "../graphics/GraphicsDevice.h"
#include "../graphics/MeshPrimitives.h"
#include "../graphics/ObjLoader.h"
#include <algorithm>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>
#include <windows.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace resources {

ResourceManager::ResourceManager(graphics::GraphicsDevice &device)
    : m_device(device),
      m_meshPool(graphics::Mesh{}) // Fallback dummy (Empty Mesh)
      ,
      m_shaderPool(graphics::Shader{}) // Fallback dummy (Empty Shader)
      ,
      m_audioPool(audio::AudioClip{}) // Fallback
{}

// ... Mesh/Shaderの実装 ...

// ===========================================
// Audio Implementation
// ===========================================

#include <wrl/client.h>

// ===========================================
// Audio Implementation (Media Foundation)
// ===========================================

AudioHandle ResourceManager::LoadAudio(const std::string &path) {
  if (auto it = m_audioCache.find(path); it != m_audioCache.end()) {
    return it->second;
  }

  // MF初期化 (スレッドセーフではないが、メインスレッドからの呼び出しを想定)
  static bool mfInitialized = false;
  if (!mfInitialized) {
    if (FAILED(MFStartup(MF_VERSION))) {
      LOG_ERROR("Resource", "MFStartup failed");
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
      size_t currentSize = clip.buffer.size();
      clip.buffer.resize(currentSize + cbBuffer);
      memcpy(clip.buffer.data() + currentSize, pAudioData, cbBuffer);
      pBuffer->Unlock();
    }
  }

  LOG_INFO("Resource", "Loaded Audio (MF): {} ({} bytes)", path,
           clip.buffer.size());

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

MeshHandle ResourceManager::LoadMesh(const std::string &path) {
  // キャッシュヒット確認
  if (auto it = m_meshCache.find(path); it != m_meshCache.end()) {
    if (m_meshPool.Get(it->second)) { // ハンドル有効性確認
      return it->second;
    }
  }

  graphics::Mesh mesh;
  bool success = false;

  // プリミティブ生成 (Registry-like approach hardcoded for now, simpler than
  // full registry)
  if (path == "builtin/cube" || path == "cube") {
    mesh = graphics::MeshPrimitives::CreateCube(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/sphere" || path == "sphere") {
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/triangle") {
    mesh = graphics::MeshPrimitives::CreateTriangle(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/plane" || path == "plane") {
    mesh =
        graphics::MeshPrimitives::CreatePlane(m_device.GetDevice(), 1.0f, 1.0f);
    success = true;
  } else if (path == "builtin/cylinder" || path == "cylinder") {
    // TODO: CreateCylinder実装後に置換。現状はsphereで代用。
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice());
    success = true;
  } else {
    // ファイル拡張子を判定してローダーを選択
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t> indices;

    // 拡張子を小文字で取得
    std::string extension;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
      extension = path.substr(dotPos);
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     ::tolower);
    }

    bool loaded = false;

    // FBX/glTF/3DS/DAE等はFbxLoader(Assimp)を使用
    if (extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
        extension == ".3ds" || extension == ".dae" || extension == ".blend") {
      loaded = graphics::FbxLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "FBX/Assimp Load failed: {}", path.c_str());
      }
    }
    // OBJファイルは専用ローダーを使用
    else if (extension == ".obj" || extension.empty()) {
      loaded = graphics::ObjLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "OBJ Load failed: {}", path.c_str());
      }
    } else {
      // 不明な拡張子は一応Assimpで試みる
      loaded = graphics::FbxLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "Unknown format load failed: {}", path.c_str());
      }
    }

    if (loaded) {
      if (mesh.Create(m_device.GetDevice(), vertices, indices)) {
        success = true;
        LOG_INFO("Resource", "Loaded Mesh: {} ({} vertices)", path.c_str(),
                 vertices.size());
      }
    }
  }

  if (!success) {
    LOG_ERROR("Resource", "Mesh load failed or fallback triggered: {}",
              path.c_str());
    // 失敗時はCubeで代用
    mesh = graphics::MeshPrimitives::CreateCube(m_device.GetDevice());
  }

  auto handle = m_meshPool.Add(std::move(mesh));
  m_meshCache[path] = handle;
  return handle;
}

MeshHandle ResourceManager::CreateDynamicMesh(
    const std::string &name, const std::vector<graphics::Vertex> &vertices,
    const std::vector<uint32_t> &indices) {

  // 同名のキャッシュがあれば上書き（または再利用）だが、
  // 動的生成なので内容が変わっている前提で新規作成または更新を行うのが安全。
  // ここではシンプルに新規作成し、キャッシュマップを更新する。

  graphics::Mesh mesh;
  if (!mesh.Create(m_device.GetDevice(), vertices, indices)) {
    LOG_ERROR("Resource", "Failed to create dynamic mesh: {}", name);
    return {};
  }

  // もし既存の同名キャッシュがあれば、古いリソースはプールに残るが、
  // キャッシュマップの指す先は新しいものになる。
  // 本格的なエンジンの場合は参照カウント等で管理すべきだが、
  // ここではシーン遷移時の全削除(Clear)に依存する。

  auto handle = m_meshPool.Add(std::move(mesh));
  m_meshCache[name] = handle;
  
  LOG_INFO("Resource", "Created dynamic mesh: {} ({} vertices)", name, vertices.size());
  return handle;
}

ShaderHandle ResourceManager::LoadShader(const std::string &name,
                                         const std::wstring &vsPath,
                                         const std::wstring &psPath) {
  if (auto it = m_shaderCache.find(name); it != m_shaderCache.end()) {
    return it->second;
  }

  // 標準的な入力レイアウトを使用
  // 将来的には引数で指定可能にするか、シェーダーリフレクションを使用
  auto inputLayout = graphics::Shader::GetDefaultInputLayout();

  graphics::Shader shader;
  bool success = shader.LoadFromFile(m_device.GetDevice(), vsPath, "main",
                                     psPath, "main", inputLayout);

  if (!success) {
    // エラーログはShader内部で出力済み
    // 無効ハンドルのまま返す（呼び出し元でチェック推奨）
  }

  auto handle = m_shaderPool.Add(std::move(shader));
  m_shaderCache[name] = handle;
  return handle;
}

void ResourceManager::Clear() {
  m_meshPool.Clear();
  m_meshCache.clear();
  m_shaderPool.Clear();
  m_shaderCache.clear();
  m_audioPool.Clear();
  m_audioCache.clear();
}

void ResourceManager::DumpStatistics() const {
  LOG_INFO("ResourceStats", "=== Resource Statistics ===");
  LOG_INFO("ResourceStats", "Meshes: {} loaded", m_meshCache.size());
  for (const auto &[name, handle] : m_meshCache) {
    LOG_INFO("ResourceStats", "  - {} (ID:{})", name.c_str(), handle.index);
  }

  LOG_INFO("ResourceStats", "Shaders: {} loaded", m_shaderCache.size());
  for (const auto &[name, handle] : m_shaderCache) {
    LOG_INFO("ResourceStats", "  - {} (ID:{})", name.c_str(), handle.index);
  }

  LOG_INFO("ResourceStats", "Audio: {} loaded", m_audioCache.size());
  for (const auto &[name, handle] : m_audioCache) {
    LOG_INFO("ResourceStats", "  - {} (ID:{})", name.c_str(), handle.index);
  }
  LOG_INFO("ResourceStats", "===========================");
}

} // namespace resources
