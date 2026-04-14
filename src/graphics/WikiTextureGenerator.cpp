/**
 * @file WikiTextureGenerator.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
 */

#include "WikiTextureGenerator.h"
#include "../core/Logger.h"
#include <algorithm>
#include <cmath>
#include <d2d1_1.h>

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

  // 1. D2D1.1 Factory 作成
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

  // 2. DirectWrite Factory 作成
  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown **>(m_dwriteFactory.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create DWriteFactory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 3. DXGI Device を取得
  ComPtr<IDXGIDevice> dxgiDevice;
  hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to get DXGI Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 4. D2D Device 作成
  hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create D2D Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 5. D2D DeviceContext 作成
  hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                        &m_d2dContext);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen",
              "Failed to create D2D DeviceContext (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 6. テキストフォーマット作成
  // タイトル用（大きめ、セリフ体風） - 2倍サイズ
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

  LOG_INFO("WikiTexGen", "Initialized successfully");
  return true;
}

void WikiTextureGenerator::Shutdown() {
  m_offscreenBitmap.Reset();
  m_offscreenTexture.Reset();
  m_titleFormat.Reset();
  m_bodyFormat.Reset();
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

bool WikiTextureGenerator::BeginGenerateTexture(
    WikiTextureGenerationState &state, const std::wstring &title,
    const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height) {

  state = WikiTextureGenerationState(); // Reset
  state.title = title;
  state.articleText = articleText;
  state.links = links;
  state.targetPage = targetPage;
  state.requestedWidth = width;
  state.requestedHeight = height;

  // 1. テキストレイアウト作成
  state.marginX = 40.0f;
  state.currentY = 140.0f;
  float maxWidth = static_cast<float>(width) - state.marginX * 2;
  float layoutMaxHeight = 500000.0f;

  HRESULT hr = m_dwriteFactory->CreateTextLayout(
      state.articleText.c_str(), static_cast<UINT32>(state.articleText.length()),
      m_bodyFormat.Get(), maxWidth, layoutMaxHeight, &state.textLayout);

  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create TextLayout");
    return false;
  }

  hr = state.textLayout->GetMetrics(&state.textMetrics);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to GetMetrics");
    return false;
  }

  float totalHeightFloat = state.currentY + state.textMetrics.height + 200.0f;
  state.totalHeight =
      std::max(height, static_cast<uint32_t>(std::ceil(totalHeightFloat)));

  float contentWidth =
      state.textMetrics.widthIncludingTrailingWhitespace + state.marginX * 2;
  state.actualWidth =
      std::max(512u, static_cast<uint32_t>(std::ceil(contentWidth)));
  state.actualWidth = std::min(state.actualWidth, width);
  if (state.actualWidth % 2 != 0)
    state.actualWidth++;

  state.result.width = state.actualWidth;
  state.result.height = state.totalHeight;
  state.remainingHeight = state.totalHeight;
  state.currentOffsetY = 0;

  // 2. リンク解析
  state.linkMatched.assign(links.size(), false);
  for (size_t i = 0; i < links.size(); ++i) {
    const auto &linkPair = links[i];
    if (linkPair.first.empty()) continue;

    bool matched = false;
    size_t pos = articleText.find(linkPair.first);
    while (pos != std::wstring::npos) {
      DWRITE_TEXT_RANGE range = {static_cast<UINT32>(pos),
                                 static_cast<UINT32>(linkPair.first.length())};
      bool isTarget = (linkPair.second == targetPage);

      UINT32 actualCount = 0;
      state.textLayout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                   nullptr, 0, &actualCount);
      if (actualCount > 0) {
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
        state.textLayout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                     metrics.data(), actualCount, &actualCount);
        for (const auto &m : metrics) {
          LinkRegion reg;
          reg.targetPage = linkPair.second;
          reg.x = m.left + state.marginX;
          reg.y = m.top + state.currentY;
          reg.width = m.width;
          reg.height = m.height;
          reg.isTarget = isTarget;
          state.result.links.push_back(reg);
          matched = true;
        }
      }
      pos = articleText.find(linkPair.first, pos + linkPair.first.length());
    }
    state.linkMatched[i] = matched;
  }

  state.started = true;
  return true;
}

bool WikiTextureGenerator::GenerateNextTile(WikiTextureGenerationState &state) {
  if (!state.started || state.completed) return true;

  const uint32_t kMaxTileHeight = 4096;
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
  if (FAILED(hr)) return true;

  ComPtr<IDXGISurface> dxgiSurface;
  tex.As(&dxgiSurface);

  D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap1> bmp;
  hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bmpProps, &bmp);
  if (FAILED(hr)) return true;

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

    for (size_t i = 0; i < state.links.size(); ++i) {
      size_t pos = state.articleText.find(state.links[i].first);
      while (pos != std::wstring::npos) {
        DWRITE_TEXT_RANGE range = {
            static_cast<UINT32>(pos),
            static_cast<UINT32>(state.links[i].first.length())};
        bool isTarget = (state.links[i].second == state.targetPage);
        state.textLayout->SetDrawingEffect(isTarget ? state.bTarget.Get() : state.bLink.Get(), range);
        state.textLayout->SetUnderline(TRUE, range);
        state.textLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        pos = state.articleText.find(state.links[i].first, pos + 1);
      }
    }
    state.brushesInitialized = true;
  }

  D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Translation(0.0f, -static_cast<float>(state.currentOffsetY));
  m_d2dContext->SetTransform(transform);

  m_d2dContext->DrawLine(D2D1::Point2F(20.0f, 120.0f), D2D1::Point2F(width - 20.0f, 120.0f), state.bBorder.Get(), 1.0f);
  D2D1_RECT_F titleRect = D2D1::RectF(20.0f, 15.0f, width - 20.0f, 115.0f);
  m_d2dContext->DrawTextW(state.title.c_str(), static_cast<UINT32>(state.title.length()), m_titleFormat.Get(), titleRect, state.bText.Get());

  for (const auto &l : state.result.links) {
    D2D1_RECT_F r = D2D1::RectF(l.x, l.y, l.x + l.width, l.y + l.height);
    m_d2dContext->FillRectangle(r, l.isTarget ? state.bBackTarget.Get() : state.bBackLink.Get());
    if (l.isTarget) m_d2dContext->DrawRectangle(r, state.bGlow.Get(), 3.0f);
  }

  m_d2dContext->DrawTextLayout(D2D1::Point2F(state.marginX, state.currentY), state.textLayout.Get(), state.bText.Get());

  float seeAlsoY = state.currentY + state.textMetrics.height + 60.0f;
  int unmatchedCount = 0;
  float linkSpacing = 60.0f;
  for (size_t i = 0; i < state.links.size(); ++i) {
    if (!state.linkMatched[i]) {
      float lx = state.marginX + (unmatchedCount % 3) * 220.0f;
      float ly = seeAlsoY + (unmatchedCount / 3) * linkSpacing;
      if (ly > state.totalHeight - 50.0f) break;

      D2D1_RECT_F linkRect = D2D1::RectF(lx, ly, lx + 200.0f, ly + 50.0f);
      bool isTarget = (state.links[i].second == state.targetPage);
      m_d2dContext->FillRectangle(linkRect, isTarget ? state.bTarget.Get() : state.bLink.Get());

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

  m_d2dContext->EndDraw();

  ComPtr<ID3D11ShaderResourceView> srv;
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  m_d3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, &srv);

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

WikiTextureResult WikiTextureGenerator::GenerateTexture(
    const std::wstring &title, const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height) {

  WikiTextureGenerationState state;
  if (!BeginGenerateTexture(state, title, articleText, links, targetPage, width, height)) {
    return WikiTextureResult();
  }

  while (!GenerateNextTile(state)) {
    // Continue
  }

  return std::move(state.result);
}

} // namespace graphics
