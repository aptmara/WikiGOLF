/**
 * @file SkyboxTextureGeneratorImage.cpp
 * @brief SkyboxTextureGeneratorImage の実装
 */

#include "SkyboxTextureGenerator.h"
#include "SkyboxTextureGeneratorInternals.h"
#include <algorithm>
#include <cctype>
#include <combaseapi.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include <wincodec.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;


namespace graphics::skybox_detail {

using Microsoft::WRL::ComPtr;

Microsoft::WRL::ComPtr<IWICImagingFactory> GetWicFactory() {
  static std::once_flag flag;
  static Microsoft::WRL::ComPtr<IWICImagingFactory> factory;

  std::call_once(flag, []() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory));
  });

  return factory;
}

bool SaveFaceToFile(const std::vector<uint8_t> &data, int faceSize,
                    const std::wstring &path) {
  auto factory = GetWicFactory();
  if (!factory) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  HRESULT hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return false;
  }

  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    return false;
  }

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  hr = encoder->CreateNewFrame(&frame, nullptr);
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->Initialize(nullptr);
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->SetSize(faceSize, faceSize);
  if (FAILED(hr)) {
    return false;
  }

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
  hr = frame->SetPixelFormat(&format);
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->WritePixels(
      faceSize, faceSize * 4, static_cast<UINT>(data.size()),
      reinterpret_cast<BYTE *>(const_cast<uint8_t *>(data.data())));
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->Commit();
  if (FAILED(hr)) {
    return false;
  }

  hr = encoder->Commit();
  return SUCCEEDED(hr);
}

bool LoadFaceFromFile(const std::wstring &path, int targetSize,
                      std::vector<uint8_t> &outData) {
  auto factory = GetWicFactory();
  if (!factory) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  HRESULT hr = factory->CreateDecoderFromFilename(
      path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
      &decoder);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    return false;
  }

  UINT width = 0;
  UINT height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    return false;
  }

  // はじめに32bpp RGBA形式に変換
  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    return false;
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return false;
  }

  // 必要に応じてターゲットサイズにスケーリング
  IWICBitmapSource *source = converter.Get();
  Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;

  if (width != static_cast<UINT>(targetSize) ||
      height != static_cast<UINT>(targetSize)) {
    hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) {
      return false;
    }
    hr = scaler->Initialize(converter.Get(), targetSize, targetSize,
                            WICBitmapInterpolationModeHighQualityCubic);
    if (FAILED(hr)) {
      return false;
    }
    source = scaler.Get();
  }

  outData.resize(targetSize * targetSize * 4);
  hr = source->CopyPixels(nullptr, targetSize * 4,
                          static_cast<UINT>(outData.size()), outData.data());
  return SUCCEEDED(hr);
}

} // namespace graphics::skybox_detail

