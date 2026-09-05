#pragma once
/**
 * @file WikiTextureGenerator.h
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成
 *
 * D2D1でオフスクリーンレンダリングし、結果をD3D11 Texture2Dとして返す。
 * リンク位置も座標として記録する。
 */

#include <cstdint>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace graphics {

using Microsoft::WRL::ComPtr;

/**
 * @brief リンク領域情報
 */
struct LinkRegion {
  std::string targetPage; ///< 遷移先ページ名
  float x, y;             ///< テクスチャ上の位置（ピクセル）
  float width, height;    ///< サイズ（ピクセル）
  bool isTarget;          ///< 目標リンクか
};

/**
 * @brief 画像配置領域（障害物用）
 */
struct ImageRegion {
  float x, y;          ///< ピクセル座標
  float width, height; ///< サイズ
};

/**
 * @brief 見出し配置領域（段差用）
 */
struct HeadingRegion {
  float x, y;          ///< ピクセル座標
  float width, height; ///< サイズ
  int level;           ///< 見出しレベル (1=H1, 2=H2...)
};

/**
 * @brief テクスチャ生成に渡す、デコード済み画像1件分の情報。
 *        ネットワーク取得・WICデコードはメインスレッド（D3D/D2Dデバイスに
 *        触れない範囲）で事前に済ませておける。
 */
struct PendingWikiImage {
  std::vector<uint8_t> pixelsBGRA; ///< 32bpp BGRA（premultiplied alpha）ピクセルデータ
  uint32_t pixelWidth = 0;
  uint32_t pixelHeight = 0;
  std::wstring caption;
  bool isLead = false;      ///< 記事先頭の代表画像（インフォボックス相当）か
  std::wstring headingText; ///< 対応する見出しテキスト（isLead=falseの場合に使用）
};

/**
 * @brief バイト列からPendingWikiImage用のピクセルデータをデコードします。
 *        D3D/D2Dデバイスに依存しないため、バックグラウンドスレッドから呼べます。
 * @param bytes ファイルバイト列（jpg/png/gif等、WICが対応する形式）
 * @param outPixelsBGRA デコード結果（32bpp BGRA, premultiplied alpha）
 * @param outWidth デコードされた画像の幅
 * @param outHeight デコードされた画像の高さ
 * @return 成功したらtrue
 */
bool DecodeWikiImageFromMemory(const std::string &bytes,
                               std::vector<uint8_t> &outPixelsBGRA,
                               uint32_t &outWidth, uint32_t &outHeight);

/**
 * @brief Wikipedia風テクスチャ生成結果
 */
struct WikiTextureResult {
  struct Tile {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    uint32_t width;
    uint32_t height;
    float offsetY; // テクスチャ全体における開始Y位置
  };
  std::vector<Tile> tiles;

  // 後方互換性用（最初のタイルまたは結合テクスチャ、単一タイルの場合はこれを使用）
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;

  uint32_t width = 0;  // 全体幅
  uint32_t height = 0; // 全体高さ
  std::vector<LinkRegion> links;
  std::vector<ImageRegion> images;
  std::vector<HeadingRegion> headings;
};

/**
 * @brief 本文レイアウトの1区画。float画像の左右でレイアウト幅が変わるため、
 *        本文全体を単一のIDWriteTextLayoutではなく複数区画に分けて保持する。
 */
struct WikiTextSegment {
  ComPtr<IDWriteTextLayout> layout;
  float yTop = 0.0f;       ///< テクスチャ全体（連続座標）でのY開始位置
  size_t textStart = 0;    ///< state.articleText内でのグローバル開始位置
  size_t textLength = 0;
};

/**
 * @brief 実際にテクスチャへ配置された画像1件（キャプション込み）。
 */
struct WikiPlacedImage {
  ComPtr<ID2D1Bitmap> bitmap;
  ComPtr<IDWriteTextLayout> captionLayout;
  float x = 0.0f, y = 0.0f;
  float width = 0.0f, height = 0.0f;
};

struct WikiTextureGenerationState {
  std::wstring title;
  std::wstring articleText;
  std::vector<std::pair<std::wstring, std::string>> links;
  std::vector<PendingWikiImage> pendingImages;
  std::string targetPage;
  uint32_t requestedWidth = 0;
  uint32_t requestedHeight = 0;

  uint32_t actualWidth = 0;
  uint32_t totalHeight = 0;
  uint32_t remainingHeight = 0;
  uint32_t currentOffsetY = 0;
  uint32_t totalTiles = 0;
  float marginX = 40.0f;
  float currentY = 140.0f;

  // タイトル領域（本文とオーバーラップしないよう、実測した高さから動的に決定する）
  float titleTopY = 30.0f;
  float separatorY = 0.0f;
  DWRITE_TEXT_METRICS titleMetrics = {};
  ComPtr<IDWriteTextLayout> titleLayout;

  // 本文（float画像の左右で幅の異なる区画に分割される）
  std::vector<WikiTextSegment> textSegments;
  std::vector<WikiPlacedImage> placedImages;
  float contentEndY = 0.0f; ///< 本文（画像込み）が終わるY位置

  std::vector<bool> linkMatched;
  bool brushesInitialized = false;
  bool drawingEffectsApplied = false;

  ComPtr<ID2D1SolidColorBrush> bText;
  ComPtr<ID2D1SolidColorBrush> bLink;
  ComPtr<ID2D1SolidColorBrush> bTarget;
  ComPtr<ID2D1SolidColorBrush> bBorder;
  ComPtr<ID2D1SolidColorBrush> bBackLink;
  ComPtr<ID2D1SolidColorBrush> bBackTarget;
  ComPtr<ID2D1SolidColorBrush> bGlow;

  WikiTextureResult result;
  bool started = false;
  bool completed = false;
};

/**
 * @brief Wikipedia記事テクスチャ生成器
 */
class WikiTextureGenerator {
public:
  WikiTextureGenerator() = default;
  ~WikiTextureGenerator() { Shutdown(); }

  // コピー禁止
  WikiTextureGenerator(const WikiTextureGenerator &) = delete;
  WikiTextureGenerator &operator=(const WikiTextureGenerator &) = delete;

  /// @brief 初期化
  /// @param device D3D11デバイス
  /// @return 成功ならtrue
  bool Initialize(ID3D11Device *device);

  /// @brief 終了処理
  void Shutdown();

  /// @brief Wikipedia記事からテクスチャを生成
  /// @param articleText 記事本文
  /// @param links リンク情報（テキスト、遷移先）
  /// @param targetPage 目標ページ名
  /// @param width テクスチャ幅
  /// @param height テクスチャ高さ
  /// @param pendingImages 見出し・リードに対応付けて埋め込む画像（デコード済み）
  /// @return 生成結果
  WikiTextureResult GenerateTexture(
      const std::wstring &title, const std::wstring &articleText,
      const std::vector<std::pair<std::wstring, std::string>> &links,
      const std::string &targetPage, uint32_t width, uint32_t height,
      std::vector<PendingWikiImage> pendingImages = {});

  /// @brief インクリメンタル生成の開始
  bool BeginGenerateTexture(
      WikiTextureGenerationState &state, const std::wstring &title,
      const std::wstring &articleText,
      const std::vector<std::pair<std::wstring, std::string>> &links,
      const std::string &targetPage, uint32_t width, uint32_t height,
      std::vector<PendingWikiImage> pendingImages = {});

  /// @brief 次のタイルを生成。完了時は true を返す
  bool GenerateNextTile(WikiTextureGenerationState &state);

private:
  /// @brief D2Dオフスクリーンターゲット作成
  bool CreateOffscreenTarget(uint32_t width, uint32_t height);

  /// @brief デコード済みBGRAピクセル列からD2Dビットマップを作成する
  ComPtr<ID2D1Bitmap> CreateBitmapFromPixels(const uint8_t *bgra,
                                            uint32_t width, uint32_t height);

  // D2D/DWrite オブジェクト
  ComPtr<ID2D1Factory1> m_d2dFactory;
  ComPtr<ID2D1DeviceContext> m_d2dContext;
  ComPtr<ID2D1Device> m_d2dDevice;
  ComPtr<IDWriteFactory> m_dwriteFactory;
  ComPtr<IDWriteTextFormat> m_titleFormat;
  ComPtr<IDWriteTextFormat> m_bodyFormat;
  ComPtr<IDWriteTextFormat> m_captionFormat;

  // D3D11 オブジェクト
  ComPtr<ID3D11Device> m_d3dDevice;

  // オフスクリーンターゲット
  ComPtr<ID3D11Texture2D> m_offscreenTexture;
  ComPtr<ID2D1Bitmap1> m_offscreenBitmap;
};

} // namespace graphics
