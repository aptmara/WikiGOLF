/**
 * @file TextRenderer.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
 */

#include "TextRenderer.h"
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

namespace {

uint32_t FloatBits(float f) {
  uint32_t u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

size_t HashCombine(size_t seed, size_t v) {
  return seed * 1099511628211ull ^ v;
}

size_t HashColor(const DirectX::XMFLOAT4 &c) {
  size_t h = 1469598103934665603ull;
  h = HashCombine(h, FloatBits(c.x));
  h = HashCombine(h, FloatBits(c.y));
  h = HashCombine(h, FloatBits(c.z));
  h = HashCombine(h, FloatBits(c.w));
  return h;
}

size_t HashStyle(const TextStyle &s) {
  size_t h = 1469598103934665603ull;
  h = HashCombine(h, std::hash<std::string>{}(s.fontFamily));
  h = HashCombine(h, FloatBits(s.fontSize));
  h = HashCombine(h, HashColor(s.color));
  h = HashCombine(h, static_cast<size_t>(s.align));
  h = HashCombine(h, static_cast<size_t>(s.valign));
  h = HashCombine(h, s.hasShadow ? 1u : 0u);
  h = HashCombine(h, HashColor(s.shadowColor));
  h = HashCombine(h, FloatBits(s.shadowOffsetX));
  h = HashCombine(h, FloatBits(s.shadowOffsetY));
  h = HashCombine(h, HashColor(s.bgColor));
  h = HashCombine(h, FloatBits(s.cornerRadius));
  h = HashCombine(h, FloatBits(s.borderWidth));
  h = HashCombine(h, HashColor(s.borderColor));
  h = HashCombine(h, s.useGradient ? 1u : 0u);
  h = HashCombine(h, HashColor(s.bgGradientEnd));
  h = HashCombine(h, s.hasOutline ? 1u : 0u);
  h = HashCombine(h, HashColor(s.outlineColor));
  h = HashCombine(h, FloatBits(s.outlineWidth));
  return h;
}

} // namespace

size_t TextRenderer::RasterCacheKeyHash::operator()(
    const RasterCacheKey &k) const {
  size_t h = std::hash<std::wstring>{}(k.text);
  h = HashCombine(h, HashStyle(k.style));
  h = HashCombine(h, k.widthBits);
  h = HashCombine(h, k.heightBits);
  return h;
}

bool TextRenderer::Initialize(IDXGISwapChain *swapChain) {
  if (!swapChain) {
    LOG_ERROR("TextRenderer", "SwapChain is null");
    return false;
  }
  m_swapChain = swapChain;

  HRESULT hr;

  // D2D1.1ファクトリの生成
  D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
  /// @brief 山内陽: Debug実行時にD2D診断レイヤーのブレークで起動が止まらないようにする。
  options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options,
                         m_d2dFactory.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to create D2D1Factory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // DirectWriteファクトリの生成
  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown **>(m_dwriteFactory.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "Failed to create DWriteFactory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // WIC Factory 作成 (画像ロード用)
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&m_wicFactory));
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "Failed to create WICImagingFactory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // DXGIデバイスの取得
  ComPtr<IDXGIDevice> dxgiDevice;
  hr = swapChain->GetDevice(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to get DXGI Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // D2Dデバイスの生成
  ComPtr<ID2D1Device> d2dDevice;
  hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to create D2D Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // D2Dデバイスコンテキストの生成
  hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                      &m_d2dContext);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "Failed to create D2D DeviceContext (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // バックバッファをBitmapとして取得・設定
  hr = CreateTargetBitmap(swapChain);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "Failed to create target bitmap (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 各種サブシステムの初期化
  m_fontManager.Initialize(m_dwriteFactory.Get());
  m_brushCache.Initialize(m_d2dContext.Get());

  // アンチエイリアス設定
  m_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

  LOG_INFO("TextRenderer", "Initialized ({}x{}) using D2D1.1 API",
           static_cast<int>(m_width), static_cast<int>(m_height));
  return true;
}

HRESULT TextRenderer::CreateTargetBitmap(IDXGISwapChain *swapChain) {
  // バックバッファから DXGI Surface を取得
  ComPtr<IDXGISurface> dxgiBackBuffer;
  HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
  if (FAILED(hr))
    return hr;

  // サーフェスサイズ取得
  DXGI_SURFACE_DESC surfaceDesc;
  dxgiBackBuffer->GetDesc(&surfaceDesc);
  m_width = static_cast<float>(surfaceDesc.Width);
  m_height = static_cast<float>(surfaceDesc.Height);

  // Bitmap プロパティ
  D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(surfaceDesc.Format, D2D1_ALPHA_MODE_PREMULTIPLIED));

  // DXGI Surface から Bitmap を作成
  ComPtr<ID2D1Bitmap1> targetBitmap;
  hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(),
                                                 &bitmapProps, &targetBitmap);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "CreateBitmapFromDxgiSurface failed (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    ComPtr<ID3D11Device> d3dDevice;
    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&d3dDevice)))) {
      HRESULT reason = d3dDevice->GetDeviceRemovedReason();
      LOG_ERROR("TextRenderer", "D3D device removed reason: {:08X}",
                static_cast<uint32_t>(reason));
    }
    return hr;
  }

  // ターゲットとして設定
  m_d2dContext->SetTarget(targetBitmap.Get());
  return S_OK;
}

void TextRenderer::Shutdown() {
  m_brushCache.Clear();
  m_bitmapCache.clear();
  m_srvBitmapCache.clear();
  m_layoutCache.clear();
  m_rasterCache.clear();
  m_rasterCacheBytes = 0;
  m_fontManager.Shutdown();
  m_d2dContext.Reset();
  m_dwriteFactory.Reset();
  m_wicFactory.Reset();
  m_d2dFactory.Reset();
  LOG_INFO("TextRenderer", "Shutdown complete");
}

void TextRenderer::BeginDraw() {
  if (m_d2dContext) {
    if (m_drawRefCount == 0) {
      m_d2dContext->BeginDraw();
      D2D1::Matrix3x2F scaleMatrix = D2D1::Matrix3x2F::Scale(
          m_width / kVirtualWidth, m_height / kVirtualHeight);
      m_d2dContext->SetTransform(scaleMatrix);
      ++m_frameCounter;
    }
    m_drawRefCount++;
  }
}

void TextRenderer::EndDraw() {
  if (m_d2dContext) {
    m_drawRefCount--;
    if (m_drawRefCount > 0) {
      return;
    }
    if (m_drawRefCount < 0) {
      m_drawRefCount = 0;
    }

    // LOG_DEBUG("TextRenderer", "EndDraw: Flashing D2D...");
    HRESULT hr = m_d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
      LOG_WARN("TextRenderer", "D2D RenderTarget lost, recreating...");
      m_d2dContext->SetTarget(nullptr);
      if (m_swapChain) {
        // まずターゲット再作成を試みる
        HRESULT hrRecreate = CreateTargetBitmap(m_swapChain.Get());
        if (SUCCEEDED(hrRecreate)) {
          LOG_INFO("TextRenderer", "D2D RenderTarget recreated successfully");
        } else {
          LOG_ERROR("TextRenderer",
                    "Failed to recreate target bitmap (HRESULT: {:08X}). "
                    "Attempting full reset...",
                    static_cast<uint32_t>(hrRecreate));

          ComPtr<IDXGISwapChain> swapChain = m_swapChain; // 退避（念のため）
          Shutdown();
          if (Initialize(swapChain.Get())) {
            LOG_INFO("TextRenderer",
                     "Full initialization successful after loss");
          } else {
            LOG_ERROR("TextRenderer", "Full initialization failed.");
          }
        }
      }
    } else if (FAILED(hr)) {
      LOG_WARN("TextRenderer", "EndDraw failed with HRESULT: {:08X}",
               static_cast<uint32_t>(hr));
    }
    // LOG_DEBUG("TextRenderer", "EndDraw: Finished");
  }
}

void TextRenderer::FillRect(const D2D1_RECT_F &rect,
                            const DirectX::XMFLOAT4 &color) {
  if (!m_d2dContext)
    return;

  ID2D1SolidColorBrush *brush = m_brushCache.GetBrush(color);
  if (brush) {
    m_d2dContext->FillRectangle(rect, brush);
  }
}

void TextRenderer::FillRoundedRect(const D2D1_RECT_F &rect, float radius,
                                   const DirectX::XMFLOAT4 &color) {
  if (!m_d2dContext)
    return;

  ID2D1SolidColorBrush *brush = m_brushCache.GetBrush(color);
  if (brush) {
    m_d2dContext->FillRoundedRectangle(
        D2D1::RoundedRect(rect, radius, radius), brush);
  }
}

void TextRenderer::DrawRoundedRect(const D2D1_RECT_F &rect, float radius,
                                   const DirectX::XMFLOAT4 &color,
                                   float width) {
  if (!m_d2dContext || width <= 0.0f)
    return;

  ID2D1SolidColorBrush *brush = m_brushCache.GetBrush(color);
  if (brush) {
    m_d2dContext->DrawRoundedRectangle(
        D2D1::RoundedRect(rect, radius, radius), brush, width);
  }
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

void TextRenderer::RenderImage(const std::string &filePath,
                               const D2D1_RECT_F &destRect, float alpha,
                               float rotation) {
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

  // 回転変換
  D2D1::Matrix3x2F scaleMatrix = D2D1::Matrix3x2F::Scale(
      m_width / kVirtualWidth, m_height / kVirtualHeight);
  if (rotation != 0.0f) {
    float centerX = destRect.left + (destRect.right - destRect.left) * 0.5f;
    float centerY = destRect.top + (destRect.bottom - destRect.top) * 0.5f;
    D2D1::Matrix3x2F rotMatrix =
        D2D1::Matrix3x2F::Rotation(rotation, D2D1::Point2F(centerX, centerY));
    m_d2dContext->SetTransform(rotMatrix * scaleMatrix);
  }

  // 描画
  m_d2dContext->DrawBitmap(bitmap, destRect, alpha,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

  // 変換リセット
  if (rotation != 0.0f) {
    m_d2dContext->SetTransform(scaleMatrix);
  }
}

void TextRenderer::RenderImage(ID3D11ShaderResourceView *srv,
                               const D2D1_RECT_F &destRect, float alpha,
                               float rotation) {
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

  // 回転変換
  D2D1::Matrix3x2F scaleMatrix = D2D1::Matrix3x2F::Scale(
      m_width / kVirtualWidth, m_height / kVirtualHeight);
  if (rotation != 0.0f) {
    float centerX = destRect.left + (destRect.right - destRect.left) * 0.5f;
    float centerY = destRect.top + (destRect.bottom - destRect.top) * 0.5f;
    D2D1::Matrix3x2F rotMatrix =
        D2D1::Matrix3x2F::Rotation(rotation, D2D1::Point2F(centerX, centerY));
    m_d2dContext->SetTransform(rotMatrix * scaleMatrix);
  }

  // 描画
  m_d2dContext->DrawBitmap(bitmap, destRect, alpha,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

  // 変換リセット
  if (rotation != 0.0f) {
    m_d2dContext->SetTransform(scaleMatrix);
  }
}

void TextRenderer::RenderText(const std::wstring &text, const D2D1_RECT_F &rect,
                              const TextStyle &style) {
  DrawTextCore(text, rect, style);
}

IDWriteTextLayout *TextRenderer::GetOrCreateTextLayout(
    const std::wstring &text, IDWriteTextFormat *format,
    const std::string &fontFamily, float fontSize, TextAlign align,
    float maxWidth, float maxHeight) {
  if (!m_dwriteFactory || !format)
    return nullptr;

  TextLayoutKey key;
  key.text = text;
  key.fontFamily = fontFamily;
  key.fontSizeBits = FloatBits(fontSize);
  key.align = align;
  key.widthBits = FloatBits(maxWidth);
  key.heightBits = FloatBits(maxHeight);

  auto it = m_layoutCache.find(key);
  if (it != m_layoutCache.end()) {
    it->second.lastUsedFrame = m_frameCounter;
    return it->second.layout.Get();
  }

  ComPtr<IDWriteTextLayout> layout;
  HRESULT hr = m_dwriteFactory->CreateTextLayout(
      text.c_str(), static_cast<UINT32>(text.length()), format, maxWidth,
      maxHeight, &layout);
  if (FAILED(hr))
    return nullptr;

  EvictLayoutCacheIfNeeded();

  TextLayoutEntry entry;
  entry.layout = layout;
  entry.lastUsedFrame = m_frameCounter;
  auto res = m_layoutCache.emplace(std::move(key), std::move(entry));
  return res.first->second.layout.Get();
}

void TextRenderer::EvictLayoutCacheIfNeeded() {
  while (m_layoutCache.size() >= kMaxLayoutCacheEntries) {
    auto oldest = m_layoutCache.begin();
    for (auto it = m_layoutCache.begin(); it != m_layoutCache.end(); ++it) {
      if (it->second.lastUsedFrame < oldest->second.lastUsedFrame) {
        oldest = it;
      }
    }
    m_layoutCache.erase(oldest);
  }
}

void TextRenderer::DrawTextCore(const std::wstring &text,
                                const D2D1_RECT_F &rect,
                                const TextStyle &style) {
  if (!m_d2dContext) return;

  float maxWidth = rect.right - rect.left;
  float maxHeight = rect.bottom - rect.top;

  IDWriteTextFormat *format = nullptr;
  IDWriteTextLayout *layout = nullptr;
  if (!text.empty()) {
    format =
        m_fontManager.GetFormat(style.fontFamily, style.fontSize, style.align);
    if (format) {
      layout = GetOrCreateTextLayout(text, format, style.fontFamily,
                                     style.fontSize, style.align, maxWidth,
                                     maxHeight);
    }
  }

  // 背景描画 (bgColor.w > 0 の場合)
  if (style.bgColor.w > 0.0f) {
    D2D1_RECT_F bgRect = rect;

    if (layout) {
      DWRITE_TEXT_METRICS metrics;
      layout->GetMetrics(&metrics);

      float textW = metrics.width;
      float textH = metrics.height;
      float offsetX = 0.0f;
      if (style.align == TextAlign::Center) {
        offsetX = (maxWidth - textW) * 0.5f;
      } else if (style.align == TextAlign::Right) {
        offsetX = (maxWidth - textW);
      }
      bgRect.left += offsetX;
      bgRect.right = bgRect.left + textW;

      float padding = 8.0f;
      bgRect.left -= padding;
      bgRect.top -= padding * 0.5f;
      bgRect.right += padding;
      bgRect.bottom = bgRect.top + textH + padding;
    }

    // 描画
    if (style.cornerRadius > 0.1f) {
      D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
          bgRect, style.cornerRadius, style.cornerRadius);

      if (style.useGradient) {
        ComPtr<ID2D1GradientStopCollection> pGradientStops;
        D2D1_GRADIENT_STOP gradientStops[2];
        gradientStops[0].color = {style.bgColor.x, style.bgColor.y, style.bgColor.z, style.bgColor.w};
        gradientStops[0].position = 0.0f;
        gradientStops[1].color = {style.bgGradientEnd.x, style.bgGradientEnd.y, style.bgGradientEnd.z, style.bgGradientEnd.w};
        gradientStops[1].position = 1.0f;

        if (SUCCEEDED(m_d2dContext->CreateGradientStopCollection(
                gradientStops, 2, &pGradientStops))) {
          ComPtr<ID2D1LinearGradientBrush> pLinearGradientBrush;
          if (SUCCEEDED(m_d2dContext->CreateLinearGradientBrush(
                  D2D1::LinearGradientBrushProperties(
                      D2D1::Point2F(bgRect.left, bgRect.top),
                      D2D1::Point2F(bgRect.left, bgRect.bottom)),
                  pGradientStops.Get(), &pLinearGradientBrush))) {
            m_d2dContext->FillRoundedRectangle(roundedRect, pLinearGradientBrush.Get());
          }
        }
      } else {
        ID2D1SolidColorBrush *bgBrush = m_brushCache.GetBrush(style.bgColor);
        if (bgBrush) {
          m_d2dContext->FillRoundedRectangle(roundedRect, bgBrush);
        }
      }

      if (style.borderWidth > 0.0f) {
        ID2D1SolidColorBrush *borderBrush = m_brushCache.GetBrush(style.borderColor);
        if (borderBrush) {
          m_d2dContext->DrawRoundedRectangle(roundedRect, borderBrush, style.borderWidth);
        }
      }
    } else {
      if (style.useGradient) {
          ComPtr<ID2D1GradientStopCollection> pGradientStops;
          D2D1_GRADIENT_STOP gradientStops[2];
          gradientStops[0].color = {style.bgColor.x, style.bgColor.y, style.bgColor.z, style.bgColor.w};
          gradientStops[0].position = 0.0f;
          gradientStops[1].color = {style.bgGradientEnd.x, style.bgGradientEnd.y, style.bgGradientEnd.z, style.bgGradientEnd.w};
          gradientStops[1].position = 1.0f;

          if (SUCCEEDED(m_d2dContext->CreateGradientStopCollection(
                  gradientStops, 2, &pGradientStops))) {
              ComPtr<ID2D1LinearGradientBrush> pLinearGradientBrush;
              if (SUCCEEDED(m_d2dContext->CreateLinearGradientBrush(
                      D2D1::LinearGradientBrushProperties(
                          D2D1::Point2F(bgRect.left, bgRect.top),
                          D2D1::Point2F(bgRect.left, bgRect.bottom)),
                      pGradientStops.Get(), &pLinearGradientBrush))) {
                  m_d2dContext->FillRectangle(bgRect, pLinearGradientBrush.Get());
              }
          }
      } else {
          ID2D1SolidColorBrush *bgBrush = m_brushCache.GetBrush(style.bgColor);
          if (bgBrush) {
              m_d2dContext->FillRectangle(bgRect, bgBrush);
          }
      }

      if (style.borderWidth > 0.0f) {
          ID2D1SolidColorBrush *borderBrush = m_brushCache.GetBrush(style.borderColor);
          if (borderBrush) {
              m_d2dContext->DrawRectangle(bgRect, borderBrush, style.borderWidth);
          }
      }
    }
  }

  if (text.empty()) return;
  if (!format)
    return;

  // 影の描画
  if (style.hasShadow) {
    ID2D1SolidColorBrush *shadowBrush =
        m_brushCache.GetBrush(style.shadowColor);
    if (shadowBrush) {
      if (layout) {
        m_d2dContext->DrawTextLayout(
            D2D1::Point2F(rect.left + style.shadowOffsetX,
                         rect.top + style.shadowOffsetY),
            layout, shadowBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
      } else {
        D2D1_RECT_F shadowRect = rect;
        shadowRect.left += style.shadowOffsetX;
        shadowRect.top += style.shadowOffsetY;
        shadowRect.right += style.shadowOffsetX;
        shadowRect.bottom += style.shadowOffsetY;

        m_d2dContext->DrawTextW(text.c_str(), static_cast<UINT32>(text.length()),
                                format, shadowRect, shadowBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
      }
    }
  }

  // アウトラインの描画（8方向にずらして描画する簡易実装）
  if (style.hasOutline) {
    ID2D1SolidColorBrush *outlineBrush =
        m_brushCache.GetBrush(style.outlineColor);
    if (outlineBrush) {
      float offsets[][2] = {{-style.outlineWidth, 0},
                            {style.outlineWidth, 0},
                            {0, -style.outlineWidth},
                            {0, style.outlineWidth},
                            {-style.outlineWidth, -style.outlineWidth},
                            {style.outlineWidth, -style.outlineWidth},
                            {-style.outlineWidth, style.outlineWidth},
                            {style.outlineWidth, style.outlineWidth}};
      for (auto &offset : offsets) {
        if (layout) {
          m_d2dContext->DrawTextLayout(
              D2D1::Point2F(rect.left + offset[0], rect.top + offset[1]),
              layout, outlineBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
          D2D1_RECT_F outlineRect = rect;
          outlineRect.left += offset[0];
          outlineRect.top += offset[1];
          outlineRect.right += offset[0];
          outlineRect.bottom += offset[1];

          m_d2dContext->DrawTextW(text.c_str(),
                                  static_cast<UINT32>(text.length()), format,
                                  outlineRect, outlineBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
      }
    }
  }

  // 本体描画
  ID2D1SolidColorBrush *brush = m_brushCache.GetBrush(style.color);
  if (brush) {
    if (layout) {
      m_d2dContext->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout,
                                   brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else {
      m_d2dContext->DrawTextW(text.c_str(), static_cast<UINT32>(text.length()),
                              format, rect, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
  }
}

void TextRenderer::RenderText(const std::wstring &text, float x, float y,
                              const TextStyle &style) {
  D2D1_RECT_F rect = D2D1::RectF(x, y, m_width, m_height);
  RenderText(text, rect, style);
}

void TextRenderer::RenderTextCached(const std::wstring &text,
                                    const D2D1_RECT_F &rect,
                                    const TextStyle &style) {
  if (!m_d2dContext) return;

  // 空テキストの背景パネルはキャッシュせず直接描画で済ませる
  if (text.empty()) {
    DrawTextCore(text, rect, style);
    return;
  }

  const float width = rect.right - rect.left;
  const float height = rect.bottom - rect.top;
  if (width <= 0.0f || height <= 0.0f) {
    DrawTextCore(text, rect, style);
    return;
  }

  RasterCacheKey key;
  key.text = text;
  key.style = style;
  key.widthBits = FloatBits(width);
  key.heightBits = FloatBits(height);

  auto it = m_rasterCache.find(key);
  if (it == m_rasterCache.end()) {
    // 影/アウトラインのはみ出し分を吸収する余白（仮想座標系）
    float margin = 8.0f + style.borderWidth;
    if (style.hasOutline) margin += style.outlineWidth;
    if (style.hasShadow) {
      margin += (std::max)(std::abs(style.shadowOffsetX),
                           std::abs(style.shadowOffsetY));
    }
    margin = (std::max)(margin, 1.0f);

    const float scaleX = (m_width > 0.0f) ? (m_width / kVirtualWidth) : 1.0f;
    const float scaleY = (m_height > 0.0f) ? (m_height / kVirtualHeight) : 1.0f;

    const UINT32 pxW = static_cast<UINT32>(
        (std::max)(1.0f, std::ceil((width + margin * 2.0f) * scaleX)));
    const UINT32 pxH = static_cast<UINT32>(
        (std::max)(1.0f, std::ceil((height + margin * 2.0f) * scaleY)));

    const size_t approxBytes = static_cast<size_t>(pxW) * pxH * 4u;

    // 1エントリでキャッシュ上限バイト数を超える場合はキャッシュせず、
    // オフスクリーンへの切り替えすら行わずに直接描画へフォールバックする。
    if (approxBytes > kMaxRasterCacheBytes) {
      LOG_WARN("TextRenderer",
               "RenderTextCached: bitmap ({}x{}, {} bytes) exceeds raster "
               "cache byte cap, rendering directly without caching",
               pxW, pxH, approxBytes);
      DrawTextCore(text, rect, style);
      return;
    }

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> offscreen;
    HRESULT hr = m_d2dContext->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0,
                                            &props, &offscreen);
    if (FAILED(hr)) {
      LOG_WARN("TextRenderer",
               "RenderTextCached: CreateBitmap failed ({:08X}), falling back",
               static_cast<uint32_t>(hr));
      DrawTextCore(text, rect, style);
      return;
    }

    // ターゲット/変換行列/アンチエイリアスモードを退避してオフスクリーンへ切り替える。
    // オフスクリーンは透明合成前提のプリマルチプライドアルファビットマップのため、
    // ClearTypeは無効な結果になる（背景色が透明黒に汚染される）。合成に有効な
    // グレースケールへ一時的に切り替える。
    ComPtr<ID2D1Image> prevTarget;
    m_d2dContext->GetTarget(&prevTarget);
    D2D1_MATRIX_3X2_F prevTransform;
    m_d2dContext->GetTransform(&prevTransform);
    const D2D1_TEXT_ANTIALIAS_MODE prevAAMode = m_d2dContext->GetTextAntialiasMode();

    m_d2dContext->SetTarget(offscreen.Get());
    m_d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0));
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(scaleX, scaleY));
    m_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    D2D1_RECT_F localRect =
        D2D1::RectF(margin, margin, margin + width, margin + height);
    DrawTextCore(text, localRect, style);

    // ターゲット/変換行列/アンチエイリアスモードを復元
    m_d2dContext->SetTarget(prevTarget.Get());
    m_d2dContext->SetTransform(prevTransform);
    m_d2dContext->SetTextAntialiasMode(prevAAMode);

    RasterCacheEntry entry;
    entry.bitmap = offscreen;
    entry.marginVirtual = margin;
    entry.approxBytes = approxBytes;
    entry.lastUsedFrame = m_frameCounter;

    EvictRasterCacheIfNeeded(entry.approxBytes);

    m_rasterCacheBytes += entry.approxBytes;
    auto emplaceResult = m_rasterCache.emplace(std::move(key), std::move(entry));
    it = emplaceResult.first;
  } else {
    it->second.lastUsedFrame = m_frameCounter;
  }

  if (it == m_rasterCache.end() || !it->second.bitmap) {
    DrawTextCore(text, rect, style);
    return;
  }

  const float margin = it->second.marginVirtual;
  D2D1_RECT_F destRect =
      D2D1::RectF(rect.left - margin, rect.top - margin,
                 rect.left - margin + width + margin * 2.0f,
                 rect.top - margin + height + margin * 2.0f);
  m_d2dContext->DrawBitmap(it->second.bitmap.Get(), destRect, 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void TextRenderer::EvictRasterCacheIfNeeded(size_t incomingBytes) {
  while (!m_rasterCache.empty() &&
        (m_rasterCache.size() >= kMaxRasterCacheEntries ||
         m_rasterCacheBytes + incomingBytes >= kMaxRasterCacheBytes)) {
    auto oldest = m_rasterCache.begin();
    for (auto it = m_rasterCache.begin(); it != m_rasterCache.end(); ++it) {
      if (it->second.lastUsedFrame < oldest->second.lastUsedFrame) {
        oldest = it;
      }
    }
    m_rasterCacheBytes -= (std::min)(m_rasterCacheBytes, oldest->second.approxBytes);
    m_rasterCache.erase(oldest);
  }
}

} // namespace graphics
