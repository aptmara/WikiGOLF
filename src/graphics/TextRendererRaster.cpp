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

    // ビットマップが覆う範囲（rect 左上からの相対、仮想座標系）。
    // 幅や高さを省いた UI 文字は rect が画面の端まで伸びるため、そのままでは
    // 全画面に近い透明なビットマップを毎フレーム合成することになり、内蔵 GPU では
    // UI だけで十数 ms かかる。DrawTextCore が実際に描く範囲（文字の字面、背景の
    // 四角、枠線・縁取り・影）だけに切り詰める。
    float coverLeft = -margin;
    float coverTop = -margin;
    float coverRight = width + margin;
    float coverBottom = height + margin;
    if (IDWriteTextFormat *format = m_fontManager.GetFormat(
            style.fontFamily, style.fontSize, style.align)) {
      if (TextLayoutEntry *entry = GetOrCreateTextLayoutEntry(
              text, format, style.fontFamily, style.fontSize, style.align,
              width, height)) {
        if (IDWriteTextLayout *layout = entry->layout.Get()) {
          DWRITE_TEXT_METRICS metrics{};
          DWRITE_OVERHANG_METRICS overhang{};
          if (SUCCEEDED(layout->GetMetrics(&metrics)) &&
              SUCCEEDED(layout->GetOverhangMetrics(&overhang))) {
            // 字面: レイアウト枠からのはみ出し量（内側なら負）で求める
            float inkLeft = -overhang.left;
            float inkTop = -overhang.top;
            float inkRight = width + overhang.right;
            float inkBottom = height + overhang.bottom;
            // 背景の四角（DrawTextCore と同じく、文字の大きさ＋余白）
            if (style.bgColor.w > 0.0f) {
              float offsetX = 0.0f;
              if (style.align == TextAlign::Center) {
                offsetX = (width - metrics.width) * 0.5f;
              } else if (style.align == TextAlign::Right) {
                offsetX = width - metrics.width;
              }
              constexpr float kBackgroundPadding = 8.0f;
              inkLeft = (std::min)(inkLeft, offsetX - kBackgroundPadding);
              inkTop = (std::min)(inkTop, -kBackgroundPadding * 0.5f);
              inkRight = (std::max)(inkRight,
                                    offsetX + metrics.width + kBackgroundPadding);
              inkBottom = (std::max)(inkBottom,
                                     metrics.height + kBackgroundPadding * 0.5f);
            }
            coverLeft = (std::max)(coverLeft, inkLeft - margin);
            coverTop = (std::max)(coverTop, inkTop - margin);
            coverRight = (std::min)(coverRight, inkRight + margin);
            coverBottom = (std::min)(coverBottom, inkBottom + margin);
            if (coverRight <= coverLeft || coverBottom <= coverTop) {
              coverLeft = -margin;
              coverTop = -margin;
              coverRight = coverLeft + 1.0f;
              coverBottom = coverTop + 1.0f;
            }
          }
        }
      }
    }

    // オフスクリーンのラスタ密度は、実際に画面へ合成する際の一様スケールに
    // 合わせる（非一様スケールだとレターボックス時の縮小率とズレて滲む）。
    const float scaleX = ComputeUniformScale();
    const float scaleY = scaleX;

    const UINT32 pxW = static_cast<UINT32>(
        (std::max)(1.0f, std::ceil((coverRight - coverLeft) * scaleX)));
    const UINT32 pxH = static_cast<UINT32>(
        (std::max)(1.0f, std::ceil((coverBottom - coverTop) * scaleY)));

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
        D2D1::RectF(-coverLeft, -coverTop, -coverLeft + width, -coverTop + height);
    DrawTextCore(text, localRect, style);

    // ターゲット/変換行列/アンチエイリアスモードを復元
    m_d2dContext->SetTarget(prevTarget.Get());
    m_d2dContext->SetTransform(prevTransform);
    m_d2dContext->SetTextAntialiasMode(prevAAMode);

    RasterCacheEntry entry;
    entry.bitmap = offscreen;
    entry.offsetX = coverLeft;
    entry.offsetY = coverTop;
    // 画素数を切り上げた分も含め、ビットマップを引き伸ばさずに貼れる大きさにする
    entry.coverWidth = static_cast<float>(pxW) / scaleX;
    entry.coverHeight = static_cast<float>(pxH) / scaleY;
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

  const auto &cached = it->second;
  const float left = rect.left + cached.offsetX;
  const float top = rect.top + cached.offsetY;
  D2D1_RECT_F destRect =
      D2D1::RectF(left, top, left + cached.coverWidth, top + cached.coverHeight);
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
