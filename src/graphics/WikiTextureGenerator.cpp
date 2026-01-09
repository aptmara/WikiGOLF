/**
 * @file WikiTextureGenerator.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
 */

#include "WikiTextureGenerator.h"
#include "../core/Logger.h"
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
  // タイトル用（大きめ、セリフ体風）
  hr = m_dwriteFactory->CreateTextFormat(
      L"Georgia", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"ja-JP", &m_titleFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create title TextFormat");
    return false;
  }

  // 本文用
  hr = m_dwriteFactory->CreateTextFormat(
      L"Meiryo", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"ja-JP", &m_bodyFormat);
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
  // 1. D3D11テクスチャ作成（D2Dと共有可能な形式）
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
    LOG_ERROR("WikiTexGen", "Failed to create offscreen texture");
    return false;
  }

  // 2. DXGIサーフェス取得
  ComPtr<IDXGISurface> dxgiSurface;
  hr = m_offscreenTexture.As(&dxgiSurface);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to get DXGI surface");
    return false;
  }

  // 3. D2D1Bitmap1作成（オフスクリーンターゲット）
  D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  hr = m_d2dContext->CreateBitmapFromDxgiSurface(
      dxgiSurface.Get(), &bitmapProps, &m_offscreenBitmap);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create D2D bitmap from surface");
    return false;
  }

  return true;
}

WikiTextureResult WikiTextureGenerator::GenerateTexture(
    const std::wstring &title, const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height) {

  WikiTextureResult result;
  result.width = width;
  result.height = height;

  // オフスクリーンターゲット作成
  if (!CreateOffscreenTarget(width, height)) {
    LOG_ERROR("WikiTexGen", "Failed to create offscreen target");
    return result;
  }

  // D2D描画開始
  m_d2dContext->SetTarget(m_offscreenBitmap.Get());
  m_d2dContext->BeginDraw();

  // 背景クリア（Wikipedia白）
  m_d2dContext->Clear(D2D1::ColorF(0.98f, 0.98f, 0.98f, 1.0f));

  // ブラシ作成
  ComPtr<ID2D1SolidColorBrush> textBrush, linkBrush, targetBrush, borderBrush;
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.125f, 0.129f, 0.133f),
                                      &textBrush);
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.4f, 0.8f),
                                      &linkBrush);
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.1f, 0.1f),
                                      &targetBrush);
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.635f, 0.663f, 0.694f),
                                      &borderBrush);

  // ヘッダーライン
  m_d2dContext->DrawLine(D2D1::Point2F(20.0f, 60.0f),
                         D2D1::Point2F(width - 20.0f, 60.0f), borderBrush.Get(),
                         1.0f);

  // タイトル描画
  D2D1_RECT_F titleRect = D2D1::RectF(20.0f, 15.0f, width - 20.0f, 55.0f);
  m_d2dContext->DrawTextW(title.c_str(), static_cast<UINT32>(title.length()),
                          m_titleFormat.Get(), titleRect, textBrush.Get());

  // 本文描画（リンクをハイライト）
  float currentY = 80.0f;
  float marginX = 30.0f;
  // テキストレイアウト作成（本文）
  ComPtr<IDWriteTextLayout> textLayout;
  float maxWidth = static_cast<float>(width) - marginX * 2;
  float maxHeight = static_cast<float>(height) - currentY - 50.0f; // 下部余白

  HRESULT hr;

  hr = m_dwriteFactory->CreateTextLayout(
      articleText.c_str(), static_cast<UINT32>(articleText.length()),
      m_bodyFormat.Get(), maxWidth, maxHeight, &textLayout);

  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create TextLayout");
    return result;
  }

  // リンクの探索とハイライト
  std::vector<bool> linkMatched(links.size(), false);

  for (size_t i = 0; i < links.size(); ++i) {
    const auto &linkPair = links[i];
    const std::wstring &linkTitle = linkPair.first;

    // 本文からリンク文字列を検索
    size_t pos = articleText.find(linkTitle);
    while (pos != std::string::npos) {
      DWRITE_TEXT_RANGE range = {static_cast<UINT32>(pos),
                                 static_cast<UINT32>(linkTitle.length())};

      // リンク色（IUnknown*として渡す）
      bool isTarget = (linkPair.second == targetPage);
      textLayout->SetDrawingEffect(
          isTarget ? targetBrush.Get() : linkBrush.Get(), range);

      // アンダーライン等の装飾も可能だが今回は色のみ

      // 座標計算
      UINT32 actualHitTestCount = 0;
      textLayout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                   nullptr, 0, &actualHitTestCount);
      if (actualHitTestCount > 0) {
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualHitTestCount);
        float hitX, hitY;
        // メトリクス取得（hitX, hitYは未使用だが引数に必要）
        textLayout->HitTestTextRange(
            range.startPosition, range.length, 0.0f, 0.0f, // origin
            metrics.data(), actualHitTestCount, &actualHitTestCount);

        // 矩形ごとにLinkRegion作成（改行で分割される可能性があるため）
        for (const auto &m : metrics) {
          LinkRegion region;
          region.targetPage = linkPair.second;
          region.x = m.left + marginX; // Layout描画開始位置オフセット
          region.y = m.top + currentY;
          region.width = m.width;
          region.height = m.height;
          region.isTarget = isTarget;
          result.links.push_back(region);
        }
      }

      linkMatched[i] = true;
      // 次の出現を探す
      pos = articleText.find(linkTitle, pos + 1);
    }
  }

  // 本文描画
  m_d2dContext->DrawTextLayout(D2D1::Point2F(marginX, currentY),
                               textLayout.Get(), textBrush.Get());

  // マッチしなかったリンクを下部に「関連項目」として表示
  DWRITE_TEXT_METRICS textMetrics;
  textLayout->GetMetrics(&textMetrics);
  float seeAlsoY = currentY + textMetrics.height + 40.0f; // 本文の下

  // 残りスペースがあるか確認
  if (seeAlsoY < height - 50.0f) {
    float linkSpacing = 40.0f;
    int unmatchedCount = 0;

    for (size_t i = 0; i < links.size(); ++i) {
      if (!linkMatched[i]) {
        const auto &link = links[i];
        bool isTarget = (link.second == targetPage);

        // リンク表示位置
        float lx = marginX + (unmatchedCount % 3) * 150.0f;
        float ly = seeAlsoY + (unmatchedCount / 3) * linkSpacing;

        if (ly > height - 40.0f)
          break; // はみ出し防止

        // リンク背景
        D2D1_RECT_F linkRect = D2D1::RectF(lx, ly, lx + 140.0f, ly + 30.0f);
        m_d2dContext->FillRectangle(linkRect, isTarget ? targetBrush.Get()
                                                       : linkBrush.Get());

        // リンクテキスト（白）
        ComPtr<ID2D1SolidColorBrush> whiteBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                            &whiteBrush);
        // 簡易描画（TextLayout使わずDrawTextで）
        m_d2dContext->DrawTextW(link.first.c_str(),
                                static_cast<UINT32>(link.first.length()),
                                m_bodyFormat.Get(), linkRect, whiteBrush.Get());

        // リンク領域記録
        LinkRegion region;
        region.targetPage = link.second;
        region.x = lx;
        region.y = ly;
        region.width = 140.0f;
        region.height = 30.0f;
        region.isTarget = isTarget;
        result.links.push_back(region);

        unmatchedCount++;
      }
    }
  }

  // === 地形生成用：画像と見出しの配置 ===

  // 1. 画像（障害物）の配置
  // テキスト領域内にランダムに配置（実際のWikipedia画像のシミュレーション）
  ComPtr<ID2D1SolidColorBrush> imgBrush, imgBorderBrush;
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f),
                                      &imgBrush);
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.7f),
                                      &imgBorderBrush);

  // 記事の長さに応じて画像数を決定
  int imgCount = 2 + (height / 800);
  srand(width + height); // 簡易的な決定論的乱数

  for (int i = 0; i < imgCount; ++i) {
    // 右寄せ配置が多いので右側を中心に
    float w = 200.0f + (rand() % 100);
    float h = 150.0f + (rand() % 100);
    float x = width - w - marginX - (rand() % 50); // 右端から少しマージン
    float y = 150.0f + (i * 400.0f) + (rand() % 100);

    if (y + h > height - 50.0f)
      continue;

    D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);

    // 描画（テキストの上に上書き）
    m_d2dContext->FillRectangle(rect, imgBrush.Get());
    m_d2dContext->DrawRectangle(rect, imgBorderBrush.Get(), 2.0f);

    // "Image" テキスト
    ComPtr<ID2D1SolidColorBrush> captionBrush;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f),
                                        &captionBrush);
    m_d2dContext->DrawTextW(L"Image", 5, m_bodyFormat.Get(), rect,
                            captionBrush.Get());

    // 結果に記録
    ImageRegion ir;
    ir.x = x;
    ir.y = y;
    ir.width = w;
    ir.height = h;
    result.images.push_back(ir);
  }

  // 2. 見出し（段差）の配置
  // 定期的にラインを引いて見出しとする
  ComPtr<ID2D1SolidColorBrush> headingBrush;
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.96f, 1.0f),
                                      &headingBrush); // 薄い青
  ComPtr<ID2D1SolidColorBrush> headingTextBrush;
  m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f),
                                      &headingTextBrush);

  float headingY = 350.0f;
  while (headingY < height - 200.0f) {
    float h = 40.0f;
    D2D1_RECT_F rect =
        D2D1::RectF(marginX, headingY, width - marginX, headingY + h);

    // 背景（段差エリア）
    m_d2dContext->FillRectangle(rect, headingBrush.Get());

    // 下線
    m_d2dContext->DrawLine(D2D1::Point2F(marginX, headingY + h),
                           D2D1::Point2F(width - marginX, headingY + h),
                           borderBrush.Get(), 1.0f);

    // テキスト
    D2D1_RECT_F textRect = rect;
    textRect.left += 10.0f;
    m_d2dContext->DrawTextW(L"Section Heading", 15, m_titleFormat.Get(),
                            textRect, headingTextBrush.Get());

    HeadingRegion hr;
    hr.x = marginX;
    hr.y = headingY;
    hr.width = width - marginX * 2;
    hr.height = h;
    hr.level = 2; // H2相当
    result.headings.push_back(hr);

    headingY += 600.0f + (rand() % 200);
  }

  // D2D描画終了
  hr = m_d2dContext->EndDraw();
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "D2D EndDraw failed");
    return result;
  }

  // SRV作成
  ComPtr<ID3D11ShaderResourceView> srv;
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  hr = m_d3dDevice->CreateShaderResourceView(m_offscreenTexture.Get(), &srvDesc,
                                             &srv);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create SRV");
    return result;
  }

  result.texture = m_offscreenTexture;
  result.srv = srv;

  LOG_INFO("WikiTexGen", "Generated texture {}x{} with {} links", width, height,
           result.links.size());
  return result;
}

} // namespace graphics
