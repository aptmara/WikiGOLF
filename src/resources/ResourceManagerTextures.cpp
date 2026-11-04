/**
 * @file ResourceManagerTextures.cpp
 * @brief 統合リソース管理クラスの実装
*/

#include "ResourceManager.h"
#include "ResourceManagerInternals.h"
#include "../core/Logger.h"
#include "../core/Profiler.h"
#include "../graphics/FbxLoader.h"
#include "../graphics/GraphicsDevice.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

namespace resources {

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
ResourceManager::LoadTextureSRV(const std::string &path) {
  const auto startedAt = std::chrono::steady_clock::now();
  if (auto it = m_textureCache.find(path); it != m_textureCache.end()) {
    // LOG_DEBUG("Resource", "LoadTexture cache hit: {} ({} ms)", path,
    //           ElapsedMs(startedAt));
    return it->second;
  }

  // 画像ファクトリの遅延初期化
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
  LOG_INFO("Resource", "Loaded Texture: {} ({}x{}, {} ms)", path, width,
           height, ElapsedMs(startedAt));
  return srv;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
ResourceManager::LoadTextureArraySRV(const std::string &name,
                                     const std::vector<std::string> &paths) {
  PROFILE_SCOPE(std::string("Resource.TextureArray.") + name);
  if (auto it = m_textureCache.find(name); it != m_textureCache.end()) {
    return it->second;
  }

  if (paths.empty())
    return {};

  // 画像ファクトリの遅延初期化
  static Microsoft::WRL::ComPtr<IWICImagingFactory> s_factory;
  if (!s_factory) {
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&s_factory));
  }

  UINT commonWidth = 0;
  UINT commonHeight = 0;
  std::vector<const std::string *> validPaths;

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
    if (FAILED(decoder->GetFrame(0, &frame))) {
      LOG_ERROR("Resource", "Array: Failed to read frame {}", path);
      continue;
    }

    UINT w = 0;
    UINT h = 0;
    frame->GetSize(&w, &h);

    if (commonWidth == 0) {
      commonWidth = w;
      commonHeight = h;
    } else if (w != commonWidth || h != commonHeight) {
      LOG_ERROR("Resource", "Array: Size mismatch in {}. Expected {}x{}, got {}x{}", path, commonWidth, commonHeight, w, h);
      continue;
    }
    validPaths.push_back(&path);
  }

  if (validPaths.empty())
    return {};

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = commonWidth;
  desc.Height = commonHeight;
  desc.MipLevels = 1;
  desc.ArraySize = static_cast<UINT>(validPaths.size());
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = m_device.GetDevice()->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Array: CreateTexture2D failed (hr=0x{:08X})", (uint32_t)hr);
    return {};
  }

  std::vector<BYTE> pixels;
  pixels.resize(static_cast<size_t>(commonWidth) * commonHeight * 4);
  for (size_t layer = 0; layer < validPaths.size(); ++layer) {
    const auto &path = *validPaths[layer];
    int sizeNeeded =
        MultiByteToWideChar(CP_UTF8, 0, path.data(),
                            static_cast<int>(path.size()), nullptr, 0);
    std::wstring widePath(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.data(),
                        static_cast<int>(path.size()), widePath.data(),
                        sizeNeeded);

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(s_factory->CreateDecoderFromFilename(
            widePath.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(s_factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut)) ||
        FAILED(converter->CopyPixels(
            nullptr, commonWidth * 4, static_cast<UINT>(pixels.size()),
            pixels.data()))) {
      LOG_ERROR("Resource", "Array: Failed to upload layer {}", path);
      return {};
    }
    const UINT subresource =
        D3D11CalcSubresource(0, static_cast<UINT>(layer), 1);
    m_device.GetContext()->UpdateSubresource(
        texture.Get(), subresource, nullptr, pixels.data(), commonWidth * 4,
        static_cast<UINT>(pixels.size()));
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
  LOG_INFO("Resource", "Loaded TextureArray: {} (Layers:{}, {}x{})", name,
           static_cast<int>(validPaths.size()), commonWidth, commonHeight);
  return srv;
}


Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
ResourceManager::LoadTextureSRVFromMemory(const std::string &cacheKey,
                                          const void *data,
                                          size_t sizeBytes) {
  if (auto it = m_textureCache.find(cacheKey); it != m_textureCache.end()) {
    return it->second;
  }
  if (!data || sizeBytes == 0) {
    return {};
  }

  static Microsoft::WRL::ComPtr<IWICImagingFactory> s_factory;
  if (!s_factory) {
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&s_factory));
    if (FAILED(hr)) {
      LOG_ERROR("Resource",
                "Failed to create WICImagingFactory (hr=0x{:08X})",
                static_cast<uint32_t>(hr));
      return {};
    }
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  HRESULT hr = s_factory->CreateStream(&stream);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CreateStream failed for {}", cacheKey);
    return {};
  }

  hr = stream->InitializeFromMemory(
      reinterpret_cast<BYTE *>(const_cast<void *>(data)),
      static_cast<DWORD>(sizeBytes));
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "InitializeFromMemory failed for {}", cacheKey);
    return {};
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  hr = s_factory->CreateDecoderFromStream(
      stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Failed to decode in-memory texture: {} (hr=0x{:08X})",
              cacheKey, static_cast<uint32_t>(hr));
    return {};
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  decoder->GetFrame(0, &frame);

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = s_factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CreateFormatConverter failed for {}", cacheKey);
    return {};
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeMedianCut);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "Format conversion failed for {}", cacheKey);
    return {};
  }

  UINT width = 0;
  UINT height = 0;
  converter->GetSize(&width, &height);
  if (width == 0 || height == 0) {
    LOG_ERROR("Resource", "In-memory texture has invalid size: {}", cacheKey);
    return {};
  }

  const UINT stride = width * 4;
  const UINT bufferSize = stride * height;
  std::vector<BYTE> pixels(bufferSize);
  hr = converter->CopyPixels(nullptr, stride, bufferSize, pixels.data());
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CopyPixels failed for {}", cacheKey);
    return {};
  }

  // ロード用スレッドからも呼べるよう、即時コンテキストが要る GenerateMips は使わず
  // CPU で全段のミップを作って初期データとして渡す。
  std::vector<std::vector<BYTE>> mips;
  std::vector<D3D11_SUBRESOURCE_DATA> initData;
  mips.push_back(std::move(pixels));
  initData.push_back({nullptr, width * 4, 0});
  for (UINT w = width, h = height; w > 1 || h > 1;) {
    const UINT nextW = (std::max)(w / 2, 1u);
    const UINT nextH = (std::max)(h / 2, 1u);
    std::vector<BYTE> next(static_cast<size_t>(nextW) * nextH * 4);
    const std::vector<BYTE> &src = mips.back();
    for (UINT y = 0; y < nextH; ++y) {
      const UINT y0 = y * 2;
      const UINT y1 = (std::min)(y0 + 1, h - 1);
      for (UINT x = 0; x < nextW; ++x) {
        const UINT x0 = x * 2;
        const UINT x1 = (std::min)(x0 + 1, w - 1);
        for (UINT c = 0; c < 4; ++c) {
          const UINT sum = src[(y0 * w + x0) * 4 + c] + src[(y0 * w + x1) * 4 + c] +
                           src[(y1 * w + x0) * 4 + c] + src[(y1 * w + x1) * 4 + c];
          next[(y * nextW + x) * 4 + c] = static_cast<BYTE>((sum + 2) / 4);
        }
      }
    }
    mips.push_back(std::move(next));
    initData.push_back({nullptr, nextW * 4, 0});
    w = nextW;
    h = nextH;
  }
  for (size_t level = 0; level < mips.size(); ++level) {
    initData[level].pSysMem = mips[level].data();
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = static_cast<UINT>(mips.size());
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  hr = m_device.GetDevice()->CreateTexture2D(&desc, initData.data(), &texture);
  if (FAILED(hr)) {
    LOG_ERROR("Resource", "CreateTexture2D failed for {} (hr=0x{:08X})",
              cacheKey, static_cast<uint32_t>(hr));
    return {};
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = desc.Format;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MostDetailedMip = 0;
  srvDesc.Texture2D.MipLevels = desc.MipLevels;

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  hr = m_device.GetDevice()->CreateShaderResourceView(texture.Get(), &srvDesc,
                                                      &srv);
  if (FAILED(hr)) {
    LOG_ERROR("Resource",
              "CreateShaderResourceView failed for {} (hr=0x{:08X})",
              cacheKey, static_cast<uint32_t>(hr));
    return {};
  }

  m_textureCache[cacheKey] = srv;
  LOG_INFO("Resource", "Loaded In-Memory Texture: {} ({}x{}, {} mips)", cacheKey,
           width, height, desc.MipLevels);
  return srv;
}

ResourceManager::ModelTextures
ResourceManager::LoadModelTextures(const std::string &path) {
  const std::string baseColorKey = path + "#baseColor";
  const std::string normalKey = path + "#normal";

  const auto baseColorIt = m_textureCache.find(baseColorKey);
  const auto normalIt = m_textureCache.find(normalKey);
  if (baseColorIt != m_textureCache.end() && normalIt != m_textureCache.end()) {
    return {baseColorIt->second, normalIt->second};
  }

  graphics::EmbeddedModelTextures images;
  if (!graphics::FbxLoader::LoadEmbeddedTextures(path, images)) {
    LOG_ERROR("Resource", "Failed to read embedded textures: {}", path);
    return {};
  }
  if (images.baseColor.empty() || images.normal.empty()) {
    LOG_WARN("Resource", "Model is missing embedded textures: {} (baseColor={}, normal={})",
             path, images.baseColor.size(), images.normal.size());
  }

  ModelTextures textures;
  textures.baseColor = LoadTextureSRVFromMemory(baseColorKey, images.baseColor.data(),
                                                images.baseColor.size());
  textures.normal = LoadTextureSRVFromMemory(normalKey, images.normal.data(),
                                             images.normal.size());
  return textures;
}

} // namespace resources

