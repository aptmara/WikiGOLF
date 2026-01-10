/**
 * @file TextRenderer.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
 */

#include "TextRenderer.h"
#include <d2d1_1.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

bool TextRenderer::Initialize(IDXGISwapChain *swapChain) {
  if (!swapChain) {
    LOG_ERROR("TextRenderer", "SwapChain is null");
    return false;
  }
  m_swapChain = swapChain;

  HRESULT hr;

  // 1. D2D1.1 Factory 作成
  D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
  options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options,
                         m_d2dFactory.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to create D2D1Factory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 2. DirectWrite Factory 作成
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

  // 3. DXGI Device を取得
  ComPtr<IDXGIDevice> dxgiDevice;
  hr = swapChain->GetDevice(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to get DXGI Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 4. D2D Device 作成
  ComPtr<ID2D1Device> d2dDevice;
  hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer", "Failed to create D2D Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 5. D2D DeviceContext 作成
  hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                      &m_d2dContext);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "Failed to create D2D DeviceContext (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 6. バックバッファを Bitmap として取得・設定
  hr = CreateTargetBitmap(swapChain);
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "Failed to create target bitmap (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // 7. サブシステム初期化
  m_fontManager.Initialize(m_dwriteFactory.Get());
  m_brushCache.Initialize(m_d2dContext.Get());

  // 8. アンチエイリアス設定
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
    return hr;
  }

  // ターゲットとして設定
  m_d2dContext->SetTarget(targetBitmap.Get());
  return S_OK;
}

void TextRenderer::Shutdown() {
  m_brushCache.Clear();
  m_bitmapCache.clear();
  m_fontManager.Shutdown();
  m_d2dContext.Reset();
  m_dwriteFactory.Reset();
  m_wicFactory.Reset();
  m_d2dFactory.Reset();
  LOG_INFO("TextRenderer", "Shutdown complete");
}

void TextRenderer::BeginDraw() {
  if (m_d2dContext) {
    m_d2dContext->BeginDraw();
  }
}

void TextRenderer::EndDraw() {
  if (m_d2dContext) {
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

          // ターゲット再作成失敗ならフルリセット
          // Shutdownでキャッシュなども消えるが、描画不可よりはマシ
          // SwapChainポインタは一旦退避が必要（Shutdownで消えるなら）
          // しかしShutdownはメンバ変数をResetするだけ。InitializeでSwapChainを再利用する。
          // m_swapChainはComPtrなのでShutdownでResetされないように注意が必要だが
          // Shutdownの実装を見ると m_d2dContext.Reset() 等であり、m_swapChain
          // は触っていない（はず）
          // ...確認すると Shutdown() 内には m_swapChain のリセット処理がない。
          // TextRenderer.h を見ると m_swapChain はメンバ変数として追加したが
          // Shutdown には入れていないはず。 なのでそのまま Shutdown ->
          // Initialize できる。

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

bool TextRenderer::LoadBitmapFromFile(const std::string &filePath) {
  if (m_bitmapCache.find(filePath) != m_bitmapCache.end()) {
    return true; // Already loaded
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
  if (rotation != 0.0f) {
    float centerX = destRect.left + (destRect.right - destRect.left) * 0.5f;
    float centerY = destRect.top + (destRect.bottom - destRect.top) * 0.5f;
    D2D1::Matrix3x2F rotMatrix =
        D2D1::Matrix3x2F::Rotation(rotation, D2D1::Point2F(centerX, centerY));
    m_d2dContext->SetTransform(rotMatrix);
  }

  // 描画
  m_d2dContext->DrawBitmap(bitmap, destRect, alpha,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

  // 変換リセット
  if (rotation != 0.0f) {
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
  }
}

void TextRenderer::RenderImage(ID3D11ShaderResourceView *srv,
                               const D2D1_RECT_F &destRect, float alpha,
                               float rotation) {
  if (!m_d2dContext || !srv)
    return;

  // SRVからリソース取得
  ComPtr<ID3D11Resource> res;
  srv->GetResource(&res);

  ComPtr<IDXGISurface> surface;
  if (FAILED(res.As(&surface)))
    return;

  // Bitmap プロパティ
  D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE,
      D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap1> bitmap;
  HRESULT hr =
      m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap);
  if (FAILED(hr))
    return;

  // 回転変換
  if (rotation != 0.0f) {
    float centerX = destRect.left + (destRect.right - destRect.left) * 0.5f;
    float centerY = destRect.top + (destRect.bottom - destRect.top) * 0.5f;
    D2D1::Matrix3x2F rotMatrix =
        D2D1::Matrix3x2F::Rotation(rotation, D2D1::Point2F(centerX, centerY));
    m_d2dContext->SetTransform(rotMatrix);
  }

  // 描画
  m_d2dContext->DrawBitmap(bitmap.Get(), destRect, alpha,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

  // 変換リセット
  if (rotation != 0.0f) {
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
  }
}

void TextRenderer::RenderText(const std::wstring &text, const D2D1_RECT_F &rect,
                              const TextStyle &style) {
  if (!m_d2dContext || text.empty())
    return;

  // TextFormat 取得
  IDWriteTextFormat *format =
      m_fontManager.GetFormat(style.fontFamily, style.fontSize, style.align);
  if (!format)
    return;

  // 背景描画 (bgColor.w > 0 の場合)
  if (style.bgColor.w > 0.0f) {
    ComPtr<IDWriteTextLayout> layout;
    float maxWidth = rect.right - rect.left;
    float maxHeight = rect.bottom - rect.top;

    // レイアウト作成
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.length()), format, maxWidth,
        maxHeight, &layout);

    if (SUCCEEDED(hr)) {
      DWRITE_TEXT_METRICS metrics;
      layout->GetMetrics(&metrics);

      // 背景矩形計算
      D2D1_RECT_F bgRect = rect;

      // アラインメントに応じたXオフセット調整 (DrawTextWの挙動に合わせる)
      // ※DrawTextWはrect内で整列するが、背景はテキスト領域だけにしたい場合は計算が必要
      // ここでは簡易的にrect全体ではなく、テキストの外接矩形に近いものを計算する

      // 幅・高さを実測値に
      float textW = metrics.width; // + style.fontSize * 0.5f; // 余白
      float textH = metrics.height;

      // アラインメント調整
      float offsetX = 0.0f;
      if (style.align == TextAlign::Center) {
        offsetX = (maxWidth - textW) * 0.5f;
      } else if (style.align == TextAlign::Right) {
        offsetX = (maxWidth - textW);
      }

      bgRect.left += offsetX;
      bgRect.right = bgRect.left + textW;

      // 少し余白を追加
      float padding = 4.0f;
      bgRect.left -= padding;
      bgRect.top -= padding * 0.5f;
      bgRect.right += padding;
      bgRect.bottom = bgRect.top + textH + padding;

      // 背景描画
      ID2D1SolidColorBrush *bgBrush = m_brushCache.GetBrush(style.bgColor);
      if (bgBrush) {
        m_d2dContext->FillRectangle(bgRect, bgBrush);
      }
    }
  }

  // 影の描画
  if (style.hasShadow) {
    ID2D1SolidColorBrush *shadowBrush =
        m_brushCache.GetBrush(style.shadowColor);
    if (shadowBrush) {
      D2D1_RECT_F shadowRect = rect;
      shadowRect.left += style.shadowOffsetX;
      shadowRect.top += style.shadowOffsetY;
      shadowRect.right += style.shadowOffsetX;
      shadowRect.bottom += style.shadowOffsetY;

      m_d2dContext->DrawTextW(text.c_str(), static_cast<UINT32>(text.length()),
                              format, shadowRect, shadowBrush);
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
        D2D1_RECT_F outlineRect = rect;
        outlineRect.left += offset[0];
        outlineRect.top += offset[1];
        outlineRect.right += offset[0];
        outlineRect.bottom += offset[1];

        m_d2dContext->DrawTextW(text.c_str(),
                                static_cast<UINT32>(text.length()), format,
                                outlineRect, outlineBrush);
      }
    }
  }

  // 本体描画
  ID2D1SolidColorBrush *brush = m_brushCache.GetBrush(style.color);
  if (brush) {
    m_d2dContext->DrawTextW(text.c_str(), static_cast<UINT32>(text.length()),
                            format, rect, brush);
  }
}

void TextRenderer::RenderText(const std::wstring &text, float x, float y,
                              const TextStyle &style) {
  D2D1_RECT_F rect = D2D1::RectF(x, y, m_width, m_height);
  RenderText(text, rect, style);
}

} // namespace graphics
