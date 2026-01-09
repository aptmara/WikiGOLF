#pragma once
/**
 * @file TextRenderer.h
 * @brief Direct2D 1.1/DirectWrite を使用した高品質テキスト描画
 */

#include "../core/Logger.h"
#include "BrushCache.h"
#include "FontManager.h"
#include "TextStyle.h"
#include <DirectXMath.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi.h>
#include <map>
#include <memory>
#include <string>
#include <wincodec.h> // WIC
#include <wrl/client.h>

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief テキスト描画クラス（D2D1.1 API使用）
/// @details Direct2D 1.1/DirectWrite の初期化・管理・描画 API を提供
class TextRenderer {
public:
  TextRenderer() = default;
  ~TextRenderer() { Shutdown(); }

  // コピー禁止
  TextRenderer(const TextRenderer &) = delete;
  TextRenderer &operator=(const TextRenderer &) = delete;

  /// @brief 初期化
  /// @param swapChain D3D11 のスワップチェーン（バックバッファ取得用）
  /// @return 成功なら true
  bool Initialize(IDXGISwapChain *swapChain);

  /// @brief 終了処理
  void Shutdown();

  /// @brief フォントをロード
  /// @param fontName 登録名
  /// @param filePath フォントファイルパス
  /// @return 成功なら true
  bool LoadFont(const std::string &fontName, const std::string &filePath) {
    return m_fontManager.LoadFont(fontName, filePath);
  }

  /// @brief 描画セッション開始
  void BeginDraw();

  /// @brief 描画セッション終了
  void EndDraw();

  /// @brief 矩形を塗りつぶし
  void FillRect(const D2D1_RECT_F &rect, const DirectX::XMFLOAT4 &color);

  /// @brief 画像をロード（キャッシュ機能付き）
  bool LoadBitmapFromFile(const std::string &filePath);

  /// @brief 画像を描画
  void RenderImage(const std::string &filePath, const D2D1_RECT_F &destRect,
                   float alpha = 1.0f, float rotation = 0.0f);

  /// @brief 画像を描画 (Raw SRV)
  void RenderImage(ID3D11ShaderResourceView *srv, const D2D1_RECT_F &destRect,
                   float alpha = 1.0f, float rotation = 0.0f);

  /// @brief テキスト描画（詳細スタイル指定）
  void RenderText(const std::wstring &text, const D2D1_RECT_F &rect,
                  const TextStyle &style);

  /// @brief テキスト描画（簡易版）
  void RenderText(const std::wstring &text, float x, float y,
                  const TextStyle &style);

  /// @brief 画面サイズ取得
  float GetWidth() const { return m_width; }
  float GetHeight() const { return m_height; }

  /// @brief 有効かどうか
  bool IsValid() const { return m_d2dContext != nullptr; }

private:
  /// @brief バックバッファを D2D ターゲットとして設定
  HRESULT CreateTargetBitmap(IDXGISwapChain *swapChain);

  // D2D 1.1 オブジェクト
  ComPtr<ID2D1Factory1> m_d2dFactory;
  ComPtr<ID2D1DeviceContext> m_d2dContext;
  ComPtr<IDWriteFactory> m_dwriteFactory;
  ComPtr<IWICImagingFactory> m_wicFactory; // WIC

  // 画像キャッシュ
  std::map<std::string, ComPtr<ID2D1Bitmap1>> m_bitmapCache;

  // サブシステム
  FontManager m_fontManager;
  BrushCache m_brushCache;

  float m_width = 0.0f;
  float m_height = 0.0f;
};

} // namespace graphics
