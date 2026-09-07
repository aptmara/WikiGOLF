/**
 * @file TextRenderer.cpp
 * @brief Direct2D 1.1/DirectWrite テキスト描画の実装
*/

#include "TextRenderer.h"
#include "TextRendererInternals.h"
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace graphics {

using text_renderer_detail::HashCombine;
using text_renderer_detail::HashStyle;

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
  /** @brief : Debug実行時にD2D診断レイヤーのブレークで起動が止まらないようにする。 */
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

float TextRenderer::ComputeUniformScale() const {
  if (m_width <= 0.0f || m_height <= 0.0f) {
    return 1.0f;
  }
  return (std::min)(m_width / kVirtualWidth, m_height / kVirtualHeight);
}

D2D1::Matrix3x2F TextRenderer::ComputeVirtualToScreenTransform() const {
  const float scale = ComputeUniformScale();
  const float offsetX = (m_width - kVirtualWidth * scale) * 0.5f;
  const float offsetY = (m_height - kVirtualHeight * scale) * 0.5f;
  return D2D1::Matrix3x2F::Scale(scale, scale) *
         D2D1::Matrix3x2F::Translation(offsetX, offsetY);
}

void TextRenderer::ReleaseTargetForResize() {
  if (m_d2dContext) {
    m_d2dContext->SetTarget(nullptr);
  }
}

bool TextRenderer::RecreateTargetAfterResize() {
  if (!m_swapChain) {
    return false;
  }
  HRESULT hr = CreateTargetBitmap(m_swapChain.Get());
  if (FAILED(hr)) {
    LOG_ERROR("TextRenderer",
              "RecreateTargetAfterResize failed (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }
  return true;
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
      m_d2dContext->SetTransform(ComputeVirtualToScreenTransform());
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

void TextRenderer::FillFullScreenRect(const DirectX::XMFLOAT4 &color) {
  if (!m_d2dContext)
    return;

  D2D1_MATRIX_3X2_F prevTransform;
  m_d2dContext->GetTransform(&prevTransform);
  m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
  FillRect(D2D1::RectF(0.0f, 0.0f, m_width, m_height), color);
  m_d2dContext->SetTransform(prevTransform);
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

} // namespace graphics
