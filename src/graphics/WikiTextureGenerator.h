#pragma once
/**
 * @file WikiTextureGenerator.h
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成
 *
 * D2D1でオフスクリーンレンダリングし、結果をD3D11 Texture2Dとして返す。
 * リンク位置も座標として記録する。
 */

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
 * @brief Wikipedia風テクスチャ生成結果
 */
struct WikiTextureResult {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<LinkRegion> links;
  std::vector<ImageRegion> images;
  std::vector<HeadingRegion> headings;
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
  /// @return 生成結果
  WikiTextureResult GenerateTexture(
      const std::wstring &title, const std::wstring &articleText,
      const std::vector<std::pair<std::wstring, std::string>> &links,
      const std::string &targetPage, uint32_t width, uint32_t height);

private:
  /// @brief D2Dオフスクリーンターゲット作成
  bool CreateOffscreenTarget(uint32_t width, uint32_t height);

  // D2D/DWrite オブジェクト
  ComPtr<ID2D1Factory1> m_d2dFactory;
  ComPtr<ID2D1DeviceContext> m_d2dContext;
  ComPtr<ID2D1Device> m_d2dDevice;
  ComPtr<IDWriteFactory> m_dwriteFactory;
  ComPtr<IDWriteTextFormat> m_titleFormat;
  ComPtr<IDWriteTextFormat> m_bodyFormat;

  // D3D11 オブジェクト
  ComPtr<ID3D11Device> m_d3dDevice;

  // オフスクリーンターゲット
  ComPtr<ID3D11Texture2D> m_offscreenTexture;
  ComPtr<ID2D1Bitmap1> m_offscreenBitmap;
};

} // namespace graphics
