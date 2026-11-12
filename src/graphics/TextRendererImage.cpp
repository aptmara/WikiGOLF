/**
 * @file TextRendererImage.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
*/

#include "TextRenderer.h"
#include "TextRendererInternals.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <wincodec.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

namespace {
float FireHash(int x, int y) {
  uint32_t value = static_cast<uint32_t>(x) * 374761393u
                 + static_cast<uint32_t>(y) * 668265263u;
  value = (value ^ (value >> 13)) * 1274126177u;
  return static_cast<float>((value ^ (value >> 16)) & 65535u) / 65535.0f;
}

float FireNoise(float x, float y) {
  const int ix = static_cast<int>(std::floor(x));
  const int iy = static_cast<int>(std::floor(y));
  float fx = x - ix, fy = y - iy;
  fx = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
  fy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
  const float lower = FireHash(ix, iy) * (1 - fx) + FireHash(ix + 1, iy) * fx;
  const float upper = FireHash(ix, iy + 1) * (1 - fx) + FireHash(ix + 1, iy + 1) * fx;
  return lower * (1 - fy) + upper * fy;
}
}

void TextRenderer::RenderButtonFire(const D2D1_RECT_F &rect, float time) {
  if (!m_d2dContext) return;
  constexpr UINT width = 200, height = 75;
  if (!m_buttonFireBitmap) {
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(m_d2dContext->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                        &properties, &m_buttonFireBitmap))) return;
    m_buttonFireTime = -1.0f;
  }
  if (m_buttonFireTime != time) {
    std::array<uint8_t, width * height * 4> pixels{};
    for (UINT py = 0; py < height; ++py) {
      for (UINT px = 0; px < width; ++px) {
        const float x = px * 2.0f, y = py * 2.0f;
        const float outsideX = std::max({0.0f, 30 - x, x - 370}) / 25;
        const float outsideY = std::max(0.0f, 77 - y) / 76
                            + std::max(0.0f, y - 123) / 22;
        const float rise = std::hypot(outsideX, outsideY);
        const float interior = std::clamp(std::min({x - 30, 370 - x, y - 77, 123 - y}) / 12, 0.0f, 1.0f);
        const float mask = 1 - .86f * interior * interior * (3 - 2 * interior);
        if (rise > 1.2f) continue;
        float edge = std::clamp(std::min({x / 12, (398 - x) / 12,
                                         y / 12, (148 - y) / 12}), 0.0f, 1.0f);
        edge = edge * edge * (3 - 2 * edge);
        const float flowY = y / 64 + time * .9f;
        const float drift = FireNoise(x / 67, flowY * .5f) - .5f;
        const float warped = x / 27 + drift * (rise + .25f) * 2.0f;
        const float large = FireNoise(warped, flowY);
        const float fine = FireNoise(warped * 2.0f, flowY * 1.8f);
        const float heat = .72f - rise * 1.05f + (large - .5f) * 1.1f + (fine - .5f) * .1f - .3f;
        if (heat <= 0) continue;
        const float core = std::min(1.0f, heat * 2.1f);
        float density = std::clamp(heat * 3.5f, 0.0f, 1.0f);
        density = density * density * (3 - 2 * density);
        const float cooling = 1 - std::clamp(rise, 0.0f, 1.0f);
        const float alpha = density * (235.0f / 255) * mask * edge * cooling;
        const size_t i = (py * width + px) * 4;
        pixels[i] = static_cast<uint8_t>((12 + 95 * core * core) * alpha);
        pixels[i + 1] = static_cast<uint8_t>((55 + 174 * core) * alpha);
        pixels[i + 2] = static_cast<uint8_t>((238 + 17 * core) * alpha);
        pixels[i + 3] = static_cast<uint8_t>(255 * alpha);
      }
    }
    if (FAILED(m_buttonFireBitmap->CopyFromMemory(nullptr, pixels.data(), width * 4))) return;
    m_buttonFireTime = time;
  }
  const float sx = (rect.right - rect.left) / 340;
  const float sy = (rect.bottom - rect.top) / 48;
  const auto destination = D2D1::RectF(rect.left - 30 * sx, rect.top - 76 * sy,
                                     rect.right + 30 * sx, rect.bottom + 26 * sy);
  m_d2dContext->DrawBitmap(m_buttonFireBitmap.Get(), destination, 1.0f,
                           D2D1_INTERPOLATION_MODE_LINEAR);
}

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
  if (grayscaleTint) {
    // 色ごとに一度だけ変換した画像を使い、エフェクトを毎回通さない
    if (ID2D1Bitmap1 *tinted = GetOrCreateTintedBitmap(bitmap, tintColor)) {
      bitmap = tinted;
      grayscaleTint = false;
    }
  }
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

ID2D1Bitmap1 *TextRenderer::GetOrCreateTintedBitmap(
    ID2D1Bitmap1 *bitmap, const DirectX::XMFLOAT4 &tintColor) {
  auto toByte = [](float v) {
    return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  const uint32_t r8 = toByte(tintColor.x);
  const uint32_t g8 = toByte(tintColor.y);
  const uint32_t b8 = toByte(tintColor.z);
  const uint32_t a8 = toByte(tintColor.w);
  const TintedBitmapKey key{bitmap, (r8 << 24) | (g8 << 16) | (b8 << 8) | a8};
  if (auto it = m_tintedBitmapCache.find(key); it != m_tintedBitmapCache.end()) {
    return it->second.tinted.Get();
  }

  // 色が連続的に変わる使い方でも際限なく増えないよう上限を設ける
  constexpr size_t kMaxTintedBitmaps = 256;
  if (m_tintedBitmapCache.size() >= kMaxTintedBitmaps) {
    m_tintedBitmapCache.clear();
  }

  if (!m_imageColorMatrixEffect &&
      FAILED(m_d2dContext->CreateEffect(CLSID_D2D1ColorMatrix,
                                        &m_imageColorMatrixEffect))) {
    return nullptr;
  }

  // 目印は画面上で最大でも数十ピクセルなので、元画像を大きいまま持たず縮めておく
  constexpr float kMaxTintedSize = 128.0f;
  const auto sourceSize = bitmap->GetSize();
  if (sourceSize.width <= 0.0f || sourceSize.height <= 0.0f) {
    return nullptr;
  }
  const float scale = std::min(1.0f, kMaxTintedSize / std::max(sourceSize.width, sourceSize.height));
  const UINT32 pxW = std::max<UINT32>(1, static_cast<UINT32>(std::ceil(sourceSize.width * scale)));
  const UINT32 pxH = std::max<UINT32>(1, static_cast<UINT32>(std::ceil(sourceSize.height * scale)));

  D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  ComPtr<ID2D1Bitmap1> tinted;
  if (FAILED(m_d2dContext->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0,
                                        &props, &tinted))) {
    return nullptr;
  }

  // 不透明度は描画時に掛けるので、ここでは色だけを焼き込む（従来の行列で opacity=1 とした形）
  const float tintR = static_cast<float>(r8) / 255.0f;
  const float tintG = static_cast<float>(g8) / 255.0f;
  const float tintB = static_cast<float>(b8) / 255.0f;
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
  matrix._44 = static_cast<float>(a8) / 255.0f;
  m_imageColorMatrixEffect->SetInput(0, bitmap);
  m_imageColorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);
  m_imageColorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE,
                                     D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED);

  ComPtr<ID2D1Image> prevTarget;
  m_d2dContext->GetTarget(&prevTarget);
  D2D1_MATRIX_3X2_F prevTransform;
  m_d2dContext->GetTransform(&prevTransform);

  m_d2dContext->SetTarget(tinted.Get());
  m_d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0));
  m_d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(
      static_cast<float>(pxW) / sourceSize.width,
      static_cast<float>(pxH) / sourceSize.height));
  m_d2dContext->DrawImage(m_imageColorMatrixEffect.Get(), nullptr, nullptr,
                          D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);

  m_d2dContext->SetTarget(prevTarget.Get());
  m_d2dContext->SetTransform(prevTransform);
  m_imageColorMatrixEffect->SetInput(0, nullptr);

  TintedBitmapCacheEntry entry;
  entry.source = bitmap;
  entry.tinted = tinted;
  ID2D1Bitmap1 *result = tinted.Get();
  m_tintedBitmapCache.emplace(key, std::move(entry));
  return result;
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

