#include "ResourceManager.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"
#include "../graphics/FbxLoader.h"
#include "../graphics/GraphicsDevice.h"
#include "../graphics/MeshPrimitives.h"
#include "../graphics/ObjLoader.h"
#include <algorithm>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>
#include <vector>
#include <wincodec.h>
#include <windows.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace resources {

ResourceManager::ResourceManager(graphics::GraphicsDevice &device)
    : m_device(device),
      m_meshPool(graphics::Mesh{}) // 繝繝溘・繝輔か繝ｼ繝ｫ繝舌ャ繧ｯ逕ｨ
      ,
      m_shaderPool(graphics::Shader{}) // 繝繝溘・繝輔か繝ｼ繝ｫ繝舌ャ繧ｯ逕ｨ
      ,
      m_audioPool(audio::AudioClip{}) // 繝繝溘・繝輔か繝ｼ繝ｫ繝舌ャ繧ｯ逕ｨ
{}

// ... Mesh/Shader縺ｮ螳溯｣・...

// 髻ｳ螢ｰ讖溯・螳溯｣・

#include <wrl/client.h>

// 髻ｳ螢ｰ讖溯・螳溯｣・(Media Foundation)

AudioHandle ResourceManager::LoadAudio(const std::string &path) {
  if (auto it = m_audioCache.find(path); it != m_audioCache.end()) {
    return it->second;
  }

  // MF蛻晄悄蛹・(繧ｹ繝ｬ繝・ラ繧ｻ繝ｼ繝輔〒縺ｯ縺ｪ縺・′縲√Γ繧､繝ｳ繧ｹ繝ｬ繝・ラ縺九ｉ縺ｮ蜻ｼ縺ｳ蜃ｺ縺励ｒ諠ｳ螳・
  static bool mfInitialized = false;
  if (!mfInitialized) {
    if (FAILED(MFStartup(MF_VERSION))) {
      LOG_ERROR("Resource", "MFStartup failed");
      return {};
    }
    mfInitialized = true;
  }

  // 繝代せ螟画鋤 (UTF-8 -> Wide)
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), NULL, 0);
  std::wstring wpath(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), &wpath[0],
                      size_needed);

  // Source Reader菴懈・
  Microsoft::WRL::ComPtr<IMFSourceReader> pReader;
  HRESULT hr = MFCreateSourceReaderFromURL(
      wpath.c_str(), NULL, &pReader); // 螻樊ｧNULL縺ｧ繝・ヵ繧ｩ繝ｫ繝域嫌蜍・
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to create SourceReader for: {} (hr={:x})",
              path, (uint32_t)hr);
    return {};
  }

  // PCM繝輔か繝ｼ繝槭ャ繝医ｒ隕∵ｱ・
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

  // 螟画鋤蠕後・螳悟・縺ｪ繝輔か繝ｼ繝槭ャ繝医ｒ蜿門ｾ・
  Microsoft::WRL::ComPtr<IMFMediaType> pUncompressedAudioType;
  hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                    &pUncompressedAudioType);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to get current media type");
    return {};
  }

  // WAVEFORMATEX縺ｸ螟画鋤
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

  // 繝・・繧ｿ隱ｭ縺ｿ霎ｼ縺ｿ
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

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
ResourceManager::LoadTextureSRV(const std::string &path) {
  if (auto it = m_textureCache.find(path); it != m_textureCache.end()) {
    return it->second;
  }

  // 逕ｻ蜒上ヵ繧｡繧ｯ繝医Μ縺ｮ驕・ｻｶ蛻晄悄蛹・
  static Microsoft::WRL::ComPtr<IWICImagingFactory> s_factory;
  if (!s_factory) {
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&s_factory));
    if (FAILED(hr)) {
      LOG_ERROR("Resource", "Failed to create WICImagingFactory (hr=0x{:08X})",
                static_cast<uint32_t>(hr));
      return {};
    }
  }

  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), NULL, 0);
  std::wstring wpath(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), &wpath[0],
                      size_needed);

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  HRESULT hr = s_factory->CreateDecoderFromFilename(
      wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
      &decoder);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to decode texture: {} (hr=0x{:08X})", path,
              static_cast<uint32_t>(hr));
    return {};
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  decoder->GetFrame(0, &frame);

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = s_factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CreateFormatConverter failed for {}", path);
    return {};
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeMedianCut);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Format conversion failed for {}", path);
    return {};
  }

  UINT width = 0;
  UINT height = 0;
  converter->GetSize(&width, &height);
  if (width == 0 || height == 0) {
    LOG_ERROR("Resource", "Texture has invalid size: {}", path);
    return {};
  }

  const UINT stride = width * 4;
  const UINT bufferSize = stride * height;
  std::vector<BYTE> pixels(bufferSize);
  hr = converter->CopyPixels(nullptr, stride, bufferSize, pixels.data());
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CopyPixels failed for {}", path);
    return {};
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA initData = {};
  initData.pSysMem = pixels.data();
  initData.SysMemPitch = stride;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  hr = m_device.GetDevice()->CreateTexture2D(&desc, &initData, &texture);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CreateTexture2D failed for {} (hr=0x{:08X})", path,
              static_cast<uint32_t>(hr));
    HRESULT reason = E_FAIL;
    if (m_device.GetDevice()) {
      reason = m_device.GetDevice()->GetDeviceRemovedReason();
    }
    if (reason != S_OK) {
      LOG_ERROR("Resource", "Device removed reason: 0x{:08X}",
                static_cast<uint32_t>(reason));
    }
    return {};
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = desc.Format;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MostDetailedMip = 0;
  srvDesc.Texture2D.MipLevels = 1;

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  hr = m_device.GetDevice()->CreateShaderResourceView(texture.Get(), &srvDesc,
                                                      &srv);
  if (FAILED(hr)) {
    LOG_ERROR("Resource",
              "CreateShaderResourceView failed for {} (hr=0x{:08X})", path,
              static_cast<uint32_t>(hr));
    return {};
  }

  m_textureCache[path] = srv;
  LOG_INFO("Resource", "Loaded Texture: {} ({}x{})", path, width, height);
  return srv;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
ResourceManager::LoadTextureArraySRV(const std::string &name,
                                     const std::vector<std::string> &paths) {
  if (auto it = m_textureCache.find(name); it != m_textureCache.end()) {
    return it->second;
  }

  if (paths.empty())
    return {};

  // 逕ｻ蜒上ヵ繧｡繧ｯ繝医Μ縺ｮ驕・ｻｶ蛻晄悄蛹・
  static Microsoft::WRL::ComPtr<IWICImagingFactory> s_factory;
  if (!s_factory) {
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&s_factory));
  }

  UINT commonWidth = 0;
  UINT commonHeight = 0;
  std::vector<std::vector<BYTE>> allPixels;

  for (const auto &path : paths) {
    int size_needed =
        MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), NULL, 0);
    std::wstring wpath(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &path[0], (int)path.size(), &wpath[0],
                        size_needed);

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(s_factory->CreateDecoderFromFilename(
            wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            &decoder))) {
      LOG_ERROR("Resource", "Array: Failed to decode {}", path);
      continue;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    s_factory->CreateFormatConverter(&converter);
    converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                          WICBitmapDitherTypeNone, nullptr, 0.0,
                          WICBitmapPaletteTypeMedianCut);

    UINT w, h;
    converter->GetSize(&w, &h);

    if (commonWidth == 0) {
      commonWidth = w;
      commonHeight = h;
    } else if (w != commonWidth || h != commonHeight) {
      LOG_ERROR("Resource", "Array: Size mismatch in {}. Expected {}x{}, got {}x{}", path, commonWidth, commonHeight, w, h);
      continue;
    }

    std::vector<BYTE> pixels(w * h * 4);
    converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());
    allPixels.push_back(std::move(pixels));
  }

  if (allPixels.empty())
    return {};

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = commonWidth;
  desc.Height = commonHeight;
  desc.MipLevels = 1;
  desc.ArraySize = (UINT)allPixels.size();
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  std::vector<D3D11_SUBRESOURCE_DATA> initData(allPixels.size());
  for (size_t i = 0; i < allPixels.size(); ++i) {
    initData[i].pSysMem = allPixels[i].data();
    initData[i].SysMemPitch = commonWidth * 4;
    initData[i].SysMemSlicePitch = (UINT)allPixels[i].size();
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = m_device.GetDevice()->CreateTexture2D(&desc, initData.data(), &texture);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Array: CreateTexture2D failed (hr=0x{:08X})", (uint32_t)hr);
    return {};
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = desc.Format;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
  srvDesc.Texture2DArray.ArraySize = desc.ArraySize;
  srvDesc.Texture2DArray.FirstArraySlice = 0;
  srvDesc.Texture2DArray.MipLevels = 1;
  srvDesc.Texture2DArray.MostDetailedMip = 0;

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  hr = m_device.GetDevice()->CreateShaderResourceView(texture.Get(), &srvDesc, &srv);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Array: CreateSRV failed (hr=0x{:08X})", (uint32_t)hr);
    return {};
  }

  m_textureCache[name] = srv;
  LOG_INFO("Resource", "Loaded TextureArray: {} (Layers:{}, {}x{})", name, (int)allPixels.size(), commonWidth, commonHeight);
  return srv;
}

MeshHandle ResourceManager::LoadMesh(const std::string &path) {
  LOG_DEBUG("Resource", "LoadMesh: START {}", path.c_str());
  // 繧ｭ繝｣繝・す繝･繝偵ャ繝育｢ｺ隱・
  if (auto it = m_meshCache.find(path); it != m_meshCache.end()) {
    LOG_DEBUG("Resource", "LoadMesh: Cache hit for {}", path.c_str());
    if (m_meshPool.Get(it->second)) { // 繝上Φ繝峨Ν譛牙柑諤ｧ遒ｺ隱・
      return it->second;
    }
    LOG_DEBUG("Resource", "LoadMesh: Cache handle invalid for {}", path.c_str());
  }

  LOG_DEBUG("Resource", "LoadMesh: Creating new mesh for {}", path.c_str());
  graphics::Mesh mesh;
  bool success = false;

  // 繧ｭ繝｣繝・す繝･縺ｫ蟄伜惠縺励↑縺・ｴ蜷医・讓呎ｺ也噪縺ｪ蜷・ｨｮ繝励Μ繝溘ユ繧｣繝悶Γ繝・す繝･繧堤函謌・
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
  } else if (path == "builtin/quad" || path == "quad") {
    mesh = graphics::MeshPrimitives::CreateQuad(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/cylinder" || path == "cylinder") {
    // TODO: CreateCylinder螳溯｣・ｾ後↓鄂ｮ謠帙ら樟迥ｶ縺ｯsphere縺ｧ莉｣逕ｨ縲・
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/torus" || path == "torus") {
    // TODO: CreateTorus螳溯｣・ｾ後↓鄂ｮ謠帙ら樟迥ｶ縺ｯsphere縺ｧ莉｣逕ｨ縲・
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice());
    success = true;
  } else {
    LOG_DEBUG("Resource", "LoadMesh: Loading from file {}", path.c_str());
    // 繝輔ぃ繧､繝ｫ諡｡蠑ｵ蟄舌ｒ蛻､螳壹＠縺ｦ繝ｭ繝ｼ繝繝ｼ繧帝∈謚・
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t> indices;

    // 諡｡蠑ｵ蟄舌ｒ蟆乗枚蟄励〒蜿門ｾ・
    std::string extension;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
      extension = path.substr(dotPos);
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     ::tolower);
    }

    bool loaded = false;

    // FBX/glTF/3DS/DAE遲峨・FbxLoader(Assimp)繧剃ｽｿ逕ｨ
    if (extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
        extension == ".3ds" || extension == ".dae" || extension == ".blend") {
      loaded = graphics::FbxLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "FBX/Assimp Load failed: {}", path.c_str());
      }
    }
    // OBJ繝輔ぃ繧､繝ｫ縺ｯ蟆ら畑繝ｭ繝ｼ繝繝ｼ繧剃ｽｿ逕ｨ
    else if (extension == ".obj" || extension.empty()) {
      loaded = graphics::ObjLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "OBJ Load failed: {}", path.c_str());
      }
    } else {
      // 荳肴・縺ｪ諡｡蠑ｵ蟄舌・荳蠢廣ssimp縺ｧ隧ｦ縺ｿ繧・
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
    // 螟ｱ謨玲凾縺ｯCube縺ｧ莉｣逕ｨ
    mesh = graphics::MeshPrimitives::CreateCube(m_device.GetDevice());
  }

  auto handle = m_meshPool.Add(std::move(mesh));
  m_meshCache[path] = handle;
  return handle;
}

MeshHandle ResourceManager::CreateDynamicMesh(
    const std::string &name, const std::vector<graphics::Vertex> &vertices,
    const std::vector<uint32_t> &indices) {

  // 驥崎､・く繝｣繝・す繝･譎ゅ・譛譁ｰ繝・・繧ｿ縺ｧ蜍慕噪繝｡繝・す繝･繧剃ｸ頑嶌縺堺ｽ懈・

  graphics::Mesh mesh;
  if (!mesh.Create(m_device.GetDevice(), vertices, indices)) {
    LOG_ERROR("Resource", "Failed to create dynamic mesh: {}", name);
    return {};
  }

  // 繧ｭ繝｣繝・す繝･繧呈眠隕上ョ繝ｼ繧ｿ縺ｧ鄂ｮ謠帙＠縲∝商縺・ョ繝ｼ繧ｿ縺ｮ隗｣謾ｾ縺ｯ荳諡ｬ繧ｯ繝ｪ繝ｼ繝ｳ繧｢繝・・縺ｫ蟋斐・繧・

  auto handle = m_meshPool.Add(std::move(mesh));
  m_meshCache[name] = handle;

  LOG_INFO("Resource", "Created dynamic mesh: {} ({} vertices)", name,
           vertices.size());
  return handle;
}

ShaderHandle ResourceManager::LoadShader(const std::string &name,
                                         const std::wstring &vsPath,
                                         const std::wstring &psPath) {
  if (auto it = m_shaderCache.find(name); it != m_shaderCache.end()) {
    return it->second;
  }

  // 蟆・擂縺ｮ諡｡蠑ｵ諤ｧ繧定・・縺励▽縺､迴ｾ蝨ｨ縺ｯ讓呎ｺ也噪縺ｪ蜈･蜉帙Ξ繧､繧｢繧ｦ繝医ｒ菴ｿ逕ｨ縺励※繧ｳ繝ｳ繝代う繝ｫ
  auto inputLayout = graphics::Shader::GetDefaultInputLayout();

  // 繧ｳ繝ｳ繝代う繝ｫ・亥ｭ伜惠縺励↑縺代ｌ縺ｰ Assets/ 繝代せ繧偵ヵ繧ｩ繝ｼ繝ｫ繝舌ャ繧ｯ・・
  graphics::Shader shader;
  auto tryCompile = [&](const std::wstring &vs, const std::wstring &ps) {
    return shader.LoadFromFile(m_device.GetDevice(), vs, "main", ps, "main",
                               inputLayout);
  };

  std::wstring vsUsed = vsPath;
  std::wstring psUsed = psPath;
  bool success = tryCompile(vsPath, psPath);

  if (!success) {
    std::filesystem::path vsAlt = std::filesystem::path(L"Assets") / vsPath;
    std::filesystem::path psAlt = std::filesystem::path(L"Assets") / psPath;
    if (std::filesystem::exists(vsAlt) && std::filesystem::exists(psAlt)) {
      vsUsed = vsAlt.wstring();
      psUsed = psAlt.wstring();
      success = tryCompile(vsUsed, psUsed);
    }
  }

  if (!success) {
    LOG_ERROR("Resource", "Failed to compile shader: {} (VS: {}, PS: {})", name,
              core::ToString(vsUsed), core::ToString(psUsed));
    return {};
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
  m_textureCache.clear();
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
