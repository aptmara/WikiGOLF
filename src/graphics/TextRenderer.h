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
#include <unordered_map>
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

  /// @brief 仮想解像度基準のレターボックス変換を無視し、物理画面全体を塗りつぶす。
  /// @details 画面フェードや暗転オーバーレイなど「アスペクト比に関わらず必ず
  ///          画面の隅々まで覆いたい」描画専用。通常のUI要素は歪み防止のため
  ///          仮想解像度でレターボックスされるが、これらは意図的にそれを迂回する。
  void FillFullScreenRect(const DirectX::XMFLOAT4 &color);

  /// @brief 角丸矩形を塗りつぶし
  void FillRoundedRect(const D2D1_RECT_F &rect, float radius,
                       const DirectX::XMFLOAT4 &color);

  /// @brief 角丸矩形の枠線を描画
  void DrawRoundedRect(const D2D1_RECT_F &rect, float radius,
                       const DirectX::XMFLOAT4 &color, float width);

  /// @brief 画像をロード（キャッシュ機能付き）
  bool LoadBitmapFromFile(const std::string &filePath);

  /// @brief 画像を描画
  void RenderImage(const std::string &filePath, const D2D1_RECT_F &destRect,
                   float alpha = 1.0f, float rotation = 0.0f);

  /// @brief 画像を描画 (Raw SRV)
  void RenderImage(ID3D11ShaderResourceView *srv, const D2D1_RECT_F &destRect,
                   float alpha = 1.0f, float rotation = 0.0f);

  /// @brief テキスト描画（詳細スタイル指定・直接描画パス）
  void RenderText(const std::wstring &text, const D2D1_RECT_F &rect,
                  const TextStyle &style);

  /// @brief テキスト描画（簡易版）
  void RenderText(const std::wstring &text, float x, float y,
                  const TextStyle &style);

  /// @brief テキスト描画（ラスタキャッシュパス）
  /// @details 安定している（毎フレーム内容が変わらない）UIテキスト向け。
  ///          初回はオフスクリーンビットマップへ一度だけ本描画（影/8方向アウトライン/
  ///          本体/背景）を行い、以降は同一内容・同一スタイル・同一レイアウトサイズ
  ///          であればDrawBitmap一発で合成する。位置(x,y)はキャッシュ識別に含めない。
  void RenderTextCached(const std::wstring &text, const D2D1_RECT_F &rect,
                        const TextStyle &style);

  /// @brief 画面サイズ取得
  float GetWidth() const { return kVirtualWidth; }
  float GetHeight() const { return kVirtualHeight; }

  /// @brief ウィンドウ/スワップチェーンのリサイズ直前に呼ぶ。
  ///        バックバッファを参照しているD2Dターゲットを解放する
  ///        （解放しないとID3D11DXGISwapChain::ResizeBuffersが失敗する）。
  void ReleaseTargetForResize();

  /// @brief GraphicsDevice::Resize() でスワップチェーンの再生成が終わった後に呼ぶ。
  ///        新しいバックバッファからD2Dターゲットを作り直す。
  /// @return 成功なら true
  bool RecreateTargetAfterResize();

  /// @brief 有効かどうか
  bool IsValid() const { return m_d2dContext != nullptr; }

private:
  /// @brief バックバッファを D2D ターゲットとして設定
  HRESULT CreateTargetBitmap(IDXGISwapChain *swapChain);

  /// @brief 仮想解像度(kVirtualWidth x kVirtualHeight)から実バックバッファへの変換行列。
  /// @details 縦横independentに引き伸ばすとアスペクト比が16:9からずれる解像度で
  ///          UIが歪むため、縦横同一倍率（アスペクト比維持）でスケールし、
  ///          余った領域は中央寄せ（レターボックス/ピラーボックス）で吸収する。
  D2D1::Matrix3x2F ComputeVirtualToScreenTransform() const;

  /// @brief 上記と同じ倍率（スカラー値のみ）
  float ComputeUniformScale() const;

  /// @brief 背景/影/アウトライン/本体を描画する共通コア処理
  /// @details RenderText と RenderTextCached のオフスクリーン生成の両方から呼ばれる。
  void DrawTextCore(const std::wstring &text, const D2D1_RECT_F &rect,
                    const TextStyle &style);

  /// @brief IDWriteTextLayout をキャッシュ経由で取得（なければ作成）
  IDWriteTextLayout *GetOrCreateTextLayout(const std::wstring &text,
                                           IDWriteTextFormat *format,
                                           const std::string &fontFamily,
                                           float fontSize, TextAlign align,
                                           float maxWidth, float maxHeight);

  ComPtr<IDXGISwapChain> m_swapChain;

  // D2D 1.1 オブジェクト
  ComPtr<ID2D1Factory1> m_d2dFactory;
  ComPtr<ID2D1DeviceContext> m_d2dContext;
  ComPtr<IDWriteFactory> m_dwriteFactory;
  ComPtr<IWICImagingFactory> m_wicFactory; // WIC

  // 画像キャッシュ
  std::map<std::string, ComPtr<ID2D1Bitmap1>> m_bitmapCache;

  /// @brief SRVラップ用D2Dビットマップのキャッシュエントリ
  /// @details キーのID3D11Resource*が指すリソースをComPtrで保持し続けることで、
  ///          解放済みアドレスが別リソースに再利用されてキーが衝突する事態を防ぐ。
  struct SrvBitmapCacheEntry {
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID2D1Bitmap1> bitmap;
  };
  std::unordered_map<ID3D11Resource *, SrvBitmapCacheEntry> m_srvBitmapCache;

  // サブシステム
  FontManager m_fontManager;
  BrushCache m_brushCache;

  /// @brief IDWriteTextLayoutキャッシュのキー
  /// @details text/フォントファミリー/正確なfloatフォントサイズ/アラインメント/
  ///          レイアウト幅・高さの完全一致で識別する。
  struct TextLayoutKey {
    std::wstring text;
    std::string fontFamily;
    uint32_t fontSizeBits = 0;
    TextAlign align = TextAlign::Left;
    uint32_t widthBits = 0;
    uint32_t heightBits = 0;

    bool operator==(const TextLayoutKey &o) const {
      return fontSizeBits == o.fontSizeBits && widthBits == o.widthBits &&
             heightBits == o.heightBits && align == o.align &&
             fontFamily == o.fontFamily && text == o.text;
    }
  };
  struct TextLayoutKeyHash {
    size_t operator()(const TextLayoutKey &k) const {
      size_t h = std::hash<std::wstring>{}(k.text);
      h = h * 31 + std::hash<std::string>{}(k.fontFamily);
      h = h * 31 + k.fontSizeBits;
      h = h * 31 + static_cast<size_t>(k.align);
      h = h * 31 + k.widthBits;
      h = h * 31 + k.heightBits;
      return h;
    }
  };
  struct TextLayoutEntry {
    ComPtr<IDWriteTextLayout> layout;
    uint64_t lastUsedFrame = 0;
  };
  std::unordered_map<TextLayoutKey, TextLayoutEntry, TextLayoutKeyHash>
      m_layoutCache;
  static constexpr size_t kMaxLayoutCacheEntries = 512;

  /// @brief 安定テキストのラスタ（ビットマップ）キャッシュのキー
  /// @details テキスト内容 + 描画に関わる全スタイルフィールド + レイアウト
  ///          幅・高さで識別する。位置(x, y)は含めない。
  struct RasterCacheKey {
    std::wstring text;
    TextStyle style;
    uint32_t widthBits = 0;
    uint32_t heightBits = 0;

    bool operator==(const RasterCacheKey &o) const {
      return widthBits == o.widthBits && heightBits == o.heightBits &&
             text == o.text && style == o.style;
    }
  };
  struct RasterCacheKeyHash {
    size_t operator()(const RasterCacheKey &k) const;
  };
  struct RasterCacheEntry {
    ComPtr<ID2D1Bitmap1> bitmap;
    float marginVirtual = 0.0f; // オフスクリーン生成時に付与した余白（仮想座標系）
    size_t approxBytes = 0;
    uint64_t lastUsedFrame = 0;
  };
  std::unordered_map<RasterCacheKey, RasterCacheEntry, RasterCacheKeyHash>
      m_rasterCache;
  static constexpr size_t kMaxRasterCacheEntries = 160;
  static constexpr size_t kMaxRasterCacheBytes = 24u * 1024u * 1024u;
  size_t m_rasterCacheBytes = 0;

  uint64_t m_frameCounter = 0;

  void EvictLayoutCacheIfNeeded();
  /// @brief エントリ数上限、および (現在バイト数 + 挿入予定バイト数) が上限を
  ///        超えないようになるまで、最古のエントリから追い出す。
  void EvictRasterCacheIfNeeded(size_t incomingBytes);

  static constexpr float kVirtualWidth = 1280.0f;
  static constexpr float kVirtualHeight = 720.0f;

  float m_width = 0.0f;
  float m_height = 0.0f;
  int m_drawRefCount = 0;
};

} // namespace graphics
