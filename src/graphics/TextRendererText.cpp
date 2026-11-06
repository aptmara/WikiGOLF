/**
 * @file TextRendererText.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
*/

#include "TextRenderer.h"
#include "TextRendererInternals.h"
#include <algorithm>
#include <cmath>
#include <d2d1_1.h>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

using text_renderer_detail::FloatBits;

namespace {

/**
 * @brief テキストレイアウトのグリフ輪郭をD2Dジオメトリとして集めるレンダラー
 * @details ずらし描きの重ね合わせではなく輪郭そのものをストロークするため、
 *          半透明色でも濃さのムラや斜め方向の欠けが出ない。
 */
class GlyphOutlineCollector final : public IDWriteTextRenderer {
public:
  explicit GlyphOutlineCollector(ID2D1Factory *factory) : m_factory(factory) {}

  /// 集めた輪郭を1つのジオメトリグループにまとめる（無ければ nullptr）
  ComPtr<ID2D1GeometryGroup> BuildGroup() const {
    if (!m_factory || m_geometries.empty()) return nullptr;
    std::vector<ID2D1Geometry *> raw;
    raw.reserve(m_geometries.size());
    for (const auto &g : m_geometries) raw.push_back(g.Get());
    ComPtr<ID2D1GeometryGroup> group;
    if (FAILED(m_factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, raw.data(),
                                              static_cast<UINT32>(raw.size()),
                                              &group))) {
      return nullptr;
    }
    return group;
  }

  // IUnknown（スタック上でのみ使うため参照カウントは形だけ）
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **obj) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
        riid == __uuidof(IDWriteTextRenderer)) {
      *obj = static_cast<IDWriteTextRenderer *>(this);
      return S_OK;
    }
    *obj = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  // IDWritePixelSnapping
  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *disabled) override {
    *disabled = TRUE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *,
                                                DWRITE_MATRIX *transform) override {
    *transform = DWRITE_MATRIX{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *ppd) override {
    *ppd = 1.0f;
    return S_OK;
  }

  // IDWriteTextRenderer
  HRESULT STDMETHODCALLTYPE DrawGlyphRun(
      void *, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_MEASURING_MODE,
      const DWRITE_GLYPH_RUN *glyphRun, const DWRITE_GLYPH_RUN_DESCRIPTION *,
      IUnknown *) override {
    if (!glyphRun || !glyphRun->fontFace || glyphRun->glyphCount == 0) return S_OK;

    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(m_factory->CreatePathGeometry(&path))) return S_OK;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return S_OK;
    HRESULT hr = glyphRun->fontFace->GetGlyphRunOutline(
        glyphRun->fontEmSize, glyphRun->glyphIndices, glyphRun->glyphAdvances,
        glyphRun->glyphOffsets, glyphRun->glyphCount, glyphRun->isSideways,
        (glyphRun->bidiLevel % 2) != 0, sink.Get());
    sink->Close();
    if (FAILED(hr)) return S_OK;

    ComPtr<ID2D1TransformedGeometry> placed;
    if (SUCCEEDED(m_factory->CreateTransformedGeometry(
            path.Get(),
            D2D1::Matrix3x2F::Translation(baselineOriginX, baselineOriginY),
            &placed))) {
      m_geometries.push_back(placed);
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT,
                                          const DWRITE_UNDERLINE *,
                                          IUnknown *) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT,
                                              const DWRITE_STRIKETHROUGH *,
                                              IUnknown *) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DrawInlineObject(void *, FLOAT, FLOAT,
                                             IDWriteInlineObject *, BOOL, BOOL,
                                             IUnknown *) override {
    return S_OK;
  }

private:
  ID2D1Factory *m_factory = nullptr;
  std::vector<ComPtr<ID2D1Geometry>> m_geometries;
};

} // namespace

void TextRenderer::RenderText(const std::wstring &text, const D2D1_RECT_F &rect,
                              const TextStyle &style) {
  DrawTextCore(text, rect, style);
}

IDWriteTextLayout *TextRenderer::GetOrCreateTextLayout(
    const std::wstring &text, IDWriteTextFormat *format,
    const std::string &fontFamily, float fontSize, TextAlign align,
    float maxWidth, float maxHeight) {
  TextLayoutEntry *entry = GetOrCreateTextLayoutEntry(
      text, format, fontFamily, fontSize, align, maxWidth, maxHeight);
  return entry ? entry->layout.Get() : nullptr;
}

TextRenderer::TextLayoutEntry *TextRenderer::GetOrCreateTextLayoutEntry(
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
    return &it->second;
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
  return &res.first->second;
}

float TextRenderer::MeasureTextHeight(const std::wstring &text,
                                      const TextStyle &style, float maxWidth) {
  if (text.empty()) {
    return 0.0f;
  }
  IDWriteTextFormat *format =
      m_fontManager.GetFormat(style.fontFamily, style.fontSize, style.align);
  if (!format) {
    return -1.0f;
  }
  constexpr float kUnboundedHeight = 10000.0f;
  IDWriteTextLayout *layout =
      GetOrCreateTextLayout(text, format, style.fontFamily, style.fontSize,
                            style.align, maxWidth, kUnboundedHeight);
  if (!layout) {
    return -1.0f;
  }
  DWRITE_TEXT_METRICS metrics;
  if (FAILED(layout->GetMetrics(&metrics))) {
    return -1.0f;
  }
  return metrics.height;
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
  TextLayoutEntry *layoutEntry = nullptr;
  if (!text.empty()) {
    format =
        m_fontManager.GetFormat(style.fontFamily, style.fontSize, style.align);
    if (format) {
      layoutEntry = GetOrCreateTextLayoutEntry(text, format, style.fontFamily,
                                               style.fontSize, style.align,
                                               maxWidth, maxHeight);
      layout = layoutEntry ? layoutEntry->layout.Get() : nullptr;
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

  // アウトラインの描画: グリフ輪郭を太さ2倍でストロークし、外側半分を縁取りにする。
  // 本体は後から上に塗るので内側半分は隠れる。
  bool outlineDrawn = false;
  if (style.hasOutline && layout && m_d2dFactory && style.outlineWidth > 0.0f) {
    ID2D1SolidColorBrush *outlineBrush =
        m_brushCache.GetBrush(style.outlineColor);
    if (outlineBrush) {
      // 輪郭の作成（グリフごとのパス生成）は重いので、同じ位置なら前回の結果を使う。
      // 作り方は毎回と同じなので、描かれる線は変わらない。
      const uint32_t xBits = FloatBits(rect.left);
      const uint32_t yBits = FloatBits(rect.top);
      ComPtr<ID2D1Geometry> outline;
      bool haveOutline = false;
      if (layoutEntry && layoutEntry->hasOutline &&
          layoutEntry->outlineXBits == xBits &&
          layoutEntry->outlineYBits == yBits) {
        outline = layoutEntry->outline;
        haveOutline = true;
      } else {
        GlyphOutlineCollector collector(m_d2dFactory.Get());
        if (SUCCEEDED(layout->Draw(nullptr, &collector, rect.left, rect.top))) {
          outline = collector.BuildGroup();
          haveOutline = true;
          if (layoutEntry) {
            layoutEntry->outline = outline;
            layoutEntry->outlineXBits = xBits;
            layoutEntry->outlineYBits = yBits;
            layoutEntry->hasOutline = true;
          }
        }
      }
      if (haveOutline) {
        if (!m_outlineStrokeStyle) {
          m_d2dFactory->CreateStrokeStyle(
              D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,
                                          D2D1_CAP_STYLE_ROUND,
                                          D2D1_CAP_STYLE_ROUND,
                                          D2D1_LINE_JOIN_ROUND),
              nullptr, 0, &m_outlineStrokeStyle);
        }
        if (outline) {
          m_d2dContext->DrawGeometry(outline.Get(), outlineBrush,
                                     style.outlineWidth * 2.0f,
                                     m_outlineStrokeStyle.Get());
        }
        outlineDrawn = true;
      }
    }
  }

  // フォールバック: レイアウトが無い場合は8方向にずらして描画する簡易実装
  if (style.hasOutline && !outlineDrawn) {
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


} // namespace graphics

