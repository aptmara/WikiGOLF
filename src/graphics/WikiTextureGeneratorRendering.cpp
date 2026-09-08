/**
 * @file WikiTextureGeneratorRendering.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
*/

#include "WikiTextureGenerator.h"
#include <algorithm>
#include <d2d1_1.h>

#pragma comment(lib, "d2d1.lib")

namespace graphics {

bool WikiTextureGenerator::GenerateNextTile(WikiTextureGenerationState &state) {
  if (!state.started || state.completed) return true;
  if (state.htmlState) return GenerateHtmlTile(state);

  auto fail = [&] {
    m_d2dContext->SetTarget(nullptr);
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    state.result = {};
    state.failed = state.completed = true;
    return true;
  };

  // 描画負荷をフレーム分散するために最大タイル高さを半分に調整
  const uint32_t kMaxTileHeight = 512;
  uint32_t tileH = std::min(state.remainingHeight, kMaxTileHeight);
  uint32_t width = state.actualWidth;

  // ブラシ色
  D2D1::ColorF colBg(1.0f, 1.0f, 1.0f, 1.0f);
  D2D1::ColorF colText(0.125f, 0.129f, 0.133f, 1.0f);
  D2D1::ColorF colLink(0.023f, 0.270f, 0.678f, 1.0f);
  D2D1::ColorF colTarget(0.647f, 0.506f, 0.0f, 1.0f);
  D2D1::ColorF colBorder(0.8f, 0.8f, 0.8f, 1.0f);
  D2D1::ColorF colLinkBack(0.9f, 0.95f, 1.0f, 1.0f);
  D2D1::ColorF colTargetBack(1.0f, 0.98f, 0.8f, 1.0f);
  D2D1::ColorF colTargetGlow(0.647f, 0.506f, 0.0f, 0.8f);

  ComPtr<ID3D11Texture2D> tex;
  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = width;
  texDesc.Height = tileH;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &tex);
  if (FAILED(hr)) return fail();

  ComPtr<IDXGISurface> dxgiSurface;
  tex.As(&dxgiSurface);

  D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap1> bmp;
  hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bmpProps, &bmp);
  if (FAILED(hr)) return fail();

  m_d2dContext->SetTarget(bmp.Get());
  m_d2dContext->BeginDraw();
  m_d2dContext->Clear(colBg);

  if (!state.brushesInitialized) {
    m_d2dContext->CreateSolidColorBrush(colText, &state.bText);
    m_d2dContext->CreateSolidColorBrush(colLink, &state.bLink);
    m_d2dContext->CreateSolidColorBrush(colTarget, &state.bTarget);
    m_d2dContext->CreateSolidColorBrush(colBorder, &state.bBorder);
    m_d2dContext->CreateSolidColorBrush(colLinkBack, &state.bBackLink);
    m_d2dContext->CreateSolidColorBrush(colTargetBack, &state.bBackTarget);
    m_d2dContext->CreateSolidColorBrush(colTargetGlow, &state.bGlow);

    // リンク文字列の装飾（太字・下線・色）を、その文字列を含む区画へ適用する
    auto findOwningSegmentMutable =
        [&](size_t pos, size_t length) -> WikiTextSegment * {
      for (auto &seg : state.textSegments) {
        if (pos >= seg.textStart && pos + length <= seg.textStart + seg.textLength) {
          return &seg;
        }
      }
      return nullptr;
    };

    for (size_t i = 0; i < state.links.size(); ++i) {
      size_t pos = state.articleText.find(state.links[i].first);
      while (pos != std::wstring::npos) {
        const size_t linkLen = state.links[i].first.length();
        WikiTextSegment *seg = findOwningSegmentMutable(pos, linkLen);
        if (seg && seg->layout) {
          const size_t localPos = pos - seg->textStart;
          DWRITE_TEXT_RANGE range = {static_cast<UINT32>(localPos),
                                     static_cast<UINT32>(linkLen)};
          bool isTarget = (state.links[i].second == state.targetPage);
          if (isTarget) {
            seg->layout->SetDrawingEffect(state.bTarget.Get(), range);
          } else {
            seg->layout->SetDrawingEffect(state.bLink.Get(), range);
          }
          seg->layout->SetUnderline(TRUE, range);
          seg->layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        }
        pos = state.articleText.find(state.links[i].first, pos + 1);
      }
    }
    state.brushesInitialized = true;
  }

  D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Translation(0.0f, -static_cast<float>(state.currentOffsetY));
  m_d2dContext->SetTransform(transform);

  m_d2dContext->DrawTextLayout(D2D1::Point2F(state.marginX, state.titleTopY), state.titleLayout.Get(), state.bText.Get());
  m_d2dContext->DrawLine(D2D1::Point2F(state.marginX, state.separatorY), D2D1::Point2F(width - state.marginX, state.separatorY), state.bBorder.Get(), 2.0f);

  for (const auto &l : state.result.links) {
    D2D1_RECT_F r = D2D1::RectF(l.x, l.y, l.x + l.width, l.y + l.height);
    if (l.isTarget) {
      m_d2dContext->FillRectangle(r, state.bBackTarget.Get());
    } else {
      m_d2dContext->FillRectangle(r, state.bBackLink.Get());
    }
    if (l.isTarget) m_d2dContext->DrawRectangle(r, state.bGlow.Get(), 3.0f);
  }

  // 本文（float画像の左右で幅の異なる区画に分けて描画）
  for (const auto &seg : state.textSegments) {
    if (!seg.layout) continue;
    m_d2dContext->DrawTextLayout(D2D1::Point2F(state.marginX, seg.yTop), seg.layout.Get(), state.bText.Get());
  }

  // Wikipedia風の画像枠＋キャプション
  for (const auto &img : state.placedImages) {
    D2D1_RECT_F dest = D2D1::RectF(img.x, img.y, img.x + img.width, img.y + img.height);
    m_d2dContext->DrawBitmap(img.bitmap.Get(), &dest);
    m_d2dContext->DrawRectangle(dest, state.bBorder.Get(), 1.5f);
    if (img.captionLayout) {
      m_d2dContext->DrawTextLayout(
          D2D1::Point2F(img.x, img.y + img.height + 8.0f),
          img.captionLayout.Get(), state.bText.Get());
    }
  }

  // 見出し下部にWikipedia風の区切り罫線を描画
  for (const auto &h : state.result.headings) {
    float lineY = h.y + h.height + 6.0f;
    m_d2dContext->DrawLine(D2D1::Point2F(state.marginX, lineY),
                          D2D1::Point2F(width - state.marginX, lineY),
                          state.bBorder.Get(), 1.5f);
  }

  float seeAlsoY = state.contentEndY + 60.0f;
  int unmatchedCount = 0;
  float linkSpacing = 60.0f;
  for (size_t i = 0; i < state.links.size(); ++i) {
    if (!state.linkMatched[i]) {
      float lx = state.marginX + (unmatchedCount % 3) * 220.0f;
      float ly = seeAlsoY + (unmatchedCount / 3) * linkSpacing;
      if (ly > state.totalHeight - 50.0f) break;

      D2D1_RECT_F linkRect = D2D1::RectF(lx, ly, lx + 200.0f, ly + 50.0f);
      bool isTarget = (state.links[i].second == state.targetPage);
      if (isTarget) {
        m_d2dContext->FillRectangle(linkRect, state.bTarget.Get());
      } else {
        m_d2dContext->FillRectangle(linkRect, state.bLink.Get());
      }

      ComPtr<ID2D1SolidColorBrush> bWhite;
      m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &bWhite);
      m_d2dContext->DrawTextW(state.links[i].first.c_str(), static_cast<UINT32>(state.links[i].first.length()), m_bodyFormat.Get(), linkRect, bWhite.Get());

      unmatchedCount++;
      if (state.currentOffsetY == 0) {
        LinkRegion reg;
        reg.targetPage = state.links[i].second;
        reg.x = lx; reg.y = ly; reg.width = 200; reg.height = 50; reg.isTarget = isTarget;
        state.result.links.push_back(reg);
      }
    }
  }

  if (FAILED(m_d2dContext->EndDraw())) return fail();

  ComPtr<ID3D11ShaderResourceView> srv;
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  if (FAILED(m_d3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, &srv))) return fail();

  WikiTextureResult::Tile tile;
  tile.texture = tex;
  tile.srv = srv;
  tile.width = width;
  tile.height = tileH;
  tile.offsetY = static_cast<float>(state.currentOffsetY);
  state.result.tiles.push_back(tile);

  state.remainingHeight -= tileH;
  state.currentOffsetY += tileH;

  if (state.remainingHeight == 0) {
    if (!state.result.tiles.empty()) {
      state.result.texture = state.result.tiles[0].texture;
      state.result.srv = state.result.tiles[0].srv;
    }
    m_d2dContext->SetTarget(nullptr);
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    state.completed = true;
    return true;
  }

  return false;
}


} // namespace graphics

