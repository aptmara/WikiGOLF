/**
 * @file WikiTextureGeneratorDecode.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
*/

#include "WikiTextureGenerator.h"
#include <algorithm>
#include <mutex>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

namespace {
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

} // namespace

bool DecodeWikiImageFromMemory(const std::string &bytes,
                               std::vector<uint8_t> &outPixelsBGRA,
                               uint32_t &outWidth, uint32_t &outHeight) {
  if (bytes.empty()) {
    return false;
  }

  auto factory = GetWicFactory();
  if (!factory) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  HRESULT hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return false;
  }

  hr = stream->InitializeFromMemory(
      reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())),
      static_cast<DWORD>(bytes.size()));
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                        WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    return false;
  }

  UINT width = 0, height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    return false;
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return false;
  }

  outWidth = width;
  outHeight = height;
  outPixelsBGRA.assign(static_cast<size_t>(width) * height * 4, 0);
  hr = converter->CopyPixels(nullptr, width * 4,
                             static_cast<UINT>(outPixelsBGRA.size()),
                             outPixelsBGRA.data());
  if (FAILED(hr)) {
    return false;
  }

  // 透過PNG（旗・アイコン等でよくある透明背景）を白背景へ合成し、完全不透明化する。
  // 予乗算アルファのまま透明部分を残すと、アルファブレンドをしない描画経路
  // （3Dの看板など）で透明部分が黒として焼き込まれ、暗く/灰色くなってしまう。
  for (size_t i = 0; i + 3 < outPixelsBGRA.size(); i += 4) {
    const uint8_t alpha = outPixelsBGRA[i + 3];
    if (alpha < 255) {
      const int inv = 255 - alpha;
      outPixelsBGRA[i + 0] = static_cast<uint8_t>(std::min(255, outPixelsBGRA[i + 0] + inv));
      outPixelsBGRA[i + 1] = static_cast<uint8_t>(std::min(255, outPixelsBGRA[i + 1] + inv));
      outPixelsBGRA[i + 2] = static_cast<uint8_t>(std::min(255, outPixelsBGRA[i + 2] + inv));
      outPixelsBGRA[i + 3] = 255;
    }
  }

  return true;
}


} // namespace graphics

