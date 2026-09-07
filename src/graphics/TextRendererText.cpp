/**
 * @file TextRendererText.cpp
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


} // namespace graphics

