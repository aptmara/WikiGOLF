/**
 * @file ResourceManagerTextures.cpp
 * @brief 統合リソース管理クラスの実装
*/

#include "ResourceManager.h"
#include "ResourceManagerInternals.h"
#include "../core/Logger.h"
#include "../graphics/GraphicsDevice.h"
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


} // namespace resources

