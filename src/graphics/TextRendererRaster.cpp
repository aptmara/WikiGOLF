/**
 * @file TextRendererRaster.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
 */

#include "TextRenderer.h"
#include "TextRendererInternals.h"
#include <algorithm>
#include <cmath>
#include <d2d1_1.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

using text_renderer_detail::FloatBits;

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

    // オフスクリーンのラスタ密度は、実際に画面へ合成する際の一様スケールに
    // 合わせる（非一様スケールだとレターボックス時の縮小率とズレて滲む）。
    const float scaleX = ComputeUniformScale();
    const float scaleY = scaleX;

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
