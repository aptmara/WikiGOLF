/**
 * @file WikiTextureGenerator.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
*/

#include "WikiTextureGenerator.h"
#include "../core/Logger.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <thread>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace graphics {

bool WikiTextureGenerator::Initialize(ID3D11Device *device) {
  if (!device) {
    LOG_ERROR("WikiTexGen", "D3D11 Device is null");
    return false;
  }
  m_d3dDevice = device;

  HRESULT hr;

  // D2D1.1ファクトリの生成
  D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
  options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options,
                         m_d2dFactory.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create D2D1Factory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // DirectWriteファクトリの生成
  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown **>(m_dwriteFactory.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create DWriteFactory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // DXGIデバイスの取得
  ComPtr<IDXGIDevice> dxgiDevice;
  hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to get DXGI Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // D2Dデバイスの生成
  hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create D2D Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // D2Dデバイスコンテキストの生成
  hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                        &m_d2dContext);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen",
              "Failed to create D2D DeviceContext (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // タイトル描画用のテキストフォーマットの生成
  hr = m_dwriteFactory->CreateTextFormat(
      L"Georgia", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 128.0f, L"ja-JP", &m_titleFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create title TextFormat");
    return false;
  }

  // 本文用 - 2倍サイズ
  hr = m_dwriteFactory->CreateTextFormat(
      L"Meiryo", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 64.0f, L"ja-JP", &m_bodyFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create body TextFormat");
    return false;
  }

  // 画像キャプション用（本文よりやや小さめ）
  hr = m_dwriteFactory->CreateTextFormat(
      L"Meiryo", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 40.0f, L"ja-JP", &m_captionFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create caption TextFormat");
    return false;
  }

  LOG_INFO("WikiTexGen", "Initialized successfully");
  return true;
}

void WikiTextureGenerator::Shutdown() {
  m_htmlCache.clear();
  m_offscreenBitmap.Reset();
  m_offscreenTexture.Reset();
  m_titleFormat.Reset();
  m_bodyFormat.Reset();
  m_captionFormat.Reset();
  m_d2dContext.Reset();
  m_d2dDevice.Reset();
  m_dwriteFactory.Reset();
  m_d2dFactory.Reset();
  m_d3dDevice.Reset();
}

bool WikiTextureGenerator::CreateOffscreenTarget(uint32_t width,
                                                 uint32_t height) {
  // レガシー互換用（単一タイル生成で使用する場合）
  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // D2D互換形式
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; // D2Dと共有

  HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr,
                                            m_offscreenTexture.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  ComPtr<IDXGISurface> dxgiSurface;
  hr = m_offscreenTexture.As(&dxgiSurface);
  if (FAILED(hr)) {
    return false;
  }

  D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  hr = m_d2dContext->CreateBitmapFromDxgiSurface(
      dxgiSurface.Get(), &bitmapProps, &m_offscreenBitmap);
  if (FAILED(hr)) {
    return false;
  }

  return true;
}

ComPtr<ID2D1Bitmap> WikiTextureGenerator::CreateBitmapFromPixels(
    const uint8_t *bgra, uint32_t width, uint32_t height) {
  if (!bgra || width == 0 || height == 0) {
    return nullptr;
  }

  D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap> bitmap;
  HRESULT hr = m_d2dContext->CreateBitmap(D2D1::SizeU(width, height), bgra,
                                          width * 4, props, &bitmap);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create bitmap from pixels (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return nullptr;
  }
  return bitmap;
}


WikiTextureResult WikiTextureGenerator::GenerateTexture(
    const std::wstring &title, const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height,
    std::vector<PendingWikiImage> pendingImages, const std::string& articleHtml) {

  WikiTextureGenerationState state;
  if (!BeginGenerateTexture(state, title, articleText, links, targetPage,
                           width, height, pendingImages, articleHtml)) {
    return WikiTextureResult();
  }

  while (!GenerateNextTile(state)) {
    std::this_thread::yield();
  }

  if (state.failed && !articleHtml.empty())
    return GenerateTexture(title, articleText, links, targetPage, width, height, std::move(pendingImages));
  return std::move(state.result);
}

} // namespace graphics
