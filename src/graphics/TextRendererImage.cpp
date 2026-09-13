/**
 * @file TextRendererImage.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
*/

#include "TextRenderer.h"
#include "TextRendererInternals.h"
#include <algorithm>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <wincodec.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

bool TextRenderer::LoadBitmapFromFile(const std::string &filePath) {
  if (m_bitmapCache.find(filePath) != m_bitmapCache.end()) {
    return true; // すでにロード済み
  }

  if (!m_wicFactory || !m_d2dContext) {
    LOG_ERROR("TextRenderer",
              "WIC or D2D context is null during load: wic={}, d2d={}",
              (void *)m_wicFactory.Get(), (void *)m_d2dContext.Get());
    return false;
  }

  if (filePath.empty()) {
    LOG_WARN("TextRenderer", "Empty file path provided to LoadBitmapFromFile");
    return false;
  }

  LOG_INFO("TextRenderer", "Loading bitmap: {}", filePath);

  std::wstring wFilePath;
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(),
                                        (int)filePath.length(), NULL, 0);
  wFilePath.resize(size_needed);
  MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), (int)filePath.length(),
                      &wFilePath[0], size_needed);

  HRESULT hr;

  // デコーダー作成
  ComPtr<IWICBitmapDecoder> decoder;
  hr = m_wicFactory->CreateDecoderFromFilename(
      wFilePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
      &decoder);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to load image: {}", filePath);
    return false;
  }

  // フレーム取得
  ComPtr<IWICBitmapFrameDecode> source;
  hr = decoder->GetFrame(0, &source);
  if (FAILED(hr))
    return false;

  // フォーマット変換
  ComPtr<IWICFormatConverter> converter;
  hr = m_wicFactory->CreateFormatConverter(&converter);
  if (FAILED(hr))
    return false;

  hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0f,
                             WICBitmapPaletteTypeMedianCut);
  if (FAILED(hr))
    return false;

  // D2D Bitmap 作成
  ComPtr<ID2D1Bitmap1> bitmap;
  hr = m_d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr,
                                               &bitmap);
  if (FAILED(hr))
    return false;

  // キャッシュに保存
  m_bitmapCache[filePath] = bitmap;
  return true;
}

void TextRenderer::DrawImageBitmap(
    ID2D1Bitmap1 *bitmap, const D2D1_RECT_F &destRect, float alpha,
    float rotation, bool grayscaleTint,
    const DirectX::XMFLOAT4 &tintColor) {
  if (!bitmap) {
    return;
  }

  const auto virtualToScreen = ComputeVirtualToScreenTransform();
  if (!grayscaleTint) {
    if (rotation != 0.0f) {
      const float centerX = (destRect.left + destRect.right) * 0.5f;
      const float centerY = (destRect.top + destRect.bottom) * 0.5f;
      const auto rotate = D2D1::Matrix3x2F::Rotation(
          rotation, D2D1::Point2F(centerX, centerY));
      m_d2dContext->SetTransform(rotate * virtualToScreen);
    }
    m_d2dContext->DrawBitmap(bitmap, destRect, alpha,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (rotation != 0.0f) {
      m_d2dContext->SetTransform(virtualToScreen);
    }
    return;
  }

  if (!m_imageColorMatrixEffect &&
      FAILED(m_d2dContext->CreateEffect(
          CLSID_D2D1ColorMatrix, &m_imageColorMatrixEffect))) {
    return;
  }

  const float opacity = std::clamp(alpha, 0.0f, 1.0f);
  const float tintR = std::clamp(tintColor.x, 0.0f, 1.0f) * opacity;
  const float tintG = std::clamp(tintColor.y, 0.0f, 1.0f) * opacity;
  const float tintB = std::clamp(tintColor.z, 0.0f, 1.0f) * opacity;
  D2D1_MATRIX_5X4_F matrix{};
  matrix._11 = 0.2126f * tintR;
  matrix._21 = 0.7152f * tintR;
  matrix._31 = 0.0722f * tintR;
  matrix._12 = 0.2126f * tintG;
  matrix._22 = 0.7152f * tintG;
  matrix._32 = 0.0722f * tintG;
  matrix._13 = 0.2126f * tintB;
  matrix._23 = 0.7152f * tintB;
  matrix._33 = 0.0722f * tintB;
  matrix._44 = opacity * std::clamp(tintColor.w, 0.0f, 1.0f);
  m_imageColorMatrixEffect->SetInput(0, bitmap);
  m_imageColorMatrixEffect->SetValue(
      D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);
  m_imageColorMatrixEffect->SetValue(
      D2D1_COLORMATRIX_PROP_ALPHA_MODE,
      D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED);

  const auto bitmapSize = bitmap->GetSize();
  const float destWidth = destRect.right - destRect.left;
  const float destHeight = destRect.bottom - destRect.top;
  const auto sourceToDest =
      D2D1::Matrix3x2F::Scale(destWidth / bitmapSize.width,
                              destHeight / bitmapSize.height) *
      D2D1::Matrix3x2F::Translation(destRect.left, destRect.top);
  const float centerX = (destRect.left + destRect.right) * 0.5f;
  const float centerY = (destRect.top + destRect.bottom) * 0.5f;
  const auto rotate = D2D1::Matrix3x2F::Rotation(
      rotation, D2D1::Point2F(centerX, centerY));
  m_d2dContext->SetTransform(sourceToDest * rotate * virtualToScreen);
  m_d2dContext->DrawImage(m_imageColorMatrixEffect.Get());
  m_imageColorMatrixEffect->SetInput(0, nullptr);
  m_d2dContext->SetTransform(virtualToScreen);
}

void TextRenderer::RenderImage(const std::string &filePath,
                               const D2D1_RECT_F &destRect, float alpha,
                               float rotation, bool grayscaleTint,
                               const DirectX::XMFLOAT4 &tintColor) {
  if (!m_d2dContext) {
    LOG_ERROR("TextRenderer", "D2D Context is null in RenderImage");
    return;
  }

  // キャッシュから取得、なければロード試行
  auto it = m_bitmapCache.find(filePath);
  if (it == m_bitmapCache.end()) {
    if (LoadBitmapFromFile(filePath)) {
      it = m_bitmapCache.find(filePath);
    } else {
      return;
    }
  }

  ID2D1Bitmap1 *bitmap = it->second.Get();
  if (!bitmap) {
    LOG_WARN("TextRenderer", "Bitmap is null in cache for: {}", filePath);
    return;
  }

  DrawImageBitmap(bitmap, destRect, alpha, rotation, grayscaleTint, tintColor);
}

void TextRenderer::RenderImage(ID3D11ShaderResourceView *srv,
                               const D2D1_RECT_F &destRect, float alpha,
                               float rotation, bool grayscaleTint,
                               const DirectX::XMFLOAT4 &tintColor) {
  if (!m_d2dContext || !srv)
    return;

  // SRVからリソース取得（キーはSRVではなく実体のID3D11Resourceで一致判定する）
  ComPtr<ID3D11Resource> res;
  srv->GetResource(&res);
  if (!res)
    return;

  ID2D1Bitmap1 *bitmap = nullptr;
  auto it = m_srvBitmapCache.find(res.Get());
  if (it != m_srvBitmapCache.end()) {
    // テクスチャの中身はGPU側で更新され続けるため、ラップ済みビットマップを使い回す
    bitmap = it->second.bitmap.Get();
  } else {
    ComPtr<IDXGISurface> surface;
    if (FAILED(res.As(&surface)))
      return;

    // Bitmap プロパティ
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE));

    ComPtr<ID2D1Bitmap1> newBitmap;
    HRESULT hr = m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(),
                                                            &props, &newBitmap);
    if (FAILED(hr)) {
      LOG_ERROR("TextRenderer", "CreateBitmapFromDxgiSurface failed in RenderImage: {:08X}", static_cast<uint32_t>(hr));
      return;
    }

    SrvBitmapCacheEntry entry;
    entry.resource = res;
    entry.bitmap = newBitmap;
    bitmap = newBitmap.Get();
    m_srvBitmapCache.emplace(res.Get(), std::move(entry));
  }

  DrawImageBitmap(bitmap, destRect, alpha, rotation, grayscaleTint, tintColor);
}


} // namespace graphics

