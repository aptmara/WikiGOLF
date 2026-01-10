#pragma once
/**
 * @file SkyboxTextureGenerator.h
 * @brief ページテーマに基づいたスカイボックステクスチャ生成
 */

#include <DirectXMath.h>
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>


namespace graphics {

/**
 * @brief スカイボックステーマ定義
 */
enum class SkyboxTheme {
  Default,        // デフォルト青空
  HistoryAncient, // 歴史・古代
  Medieval,       // 中世・城
  ScienceTech,    // 科学・技術
  SpaceAstronomy, // 宇宙・天文
  Ocean,          // 海洋・水中
  Mountain,       // 山岳・登山
  Forest,         // 森林・ジャングル
  Desert,         // 砂漠
  Polar,          // 極地・雪
  Volcano,        // 火山
  Urban,          // 都市・建築
  Sunset,         // 夜・夕暮れ
  Sports,         // スポーツ
  Art,            // 芸術・美術
  Music,          // 音楽
  Literature,     // 文学
  Medical,        // 医療・生物
  Food,           // 食品・料理
  Religion,       // 宗教・神話
  War,            // 戦争・軍事
  Fantasy,        // ファンタジー
  Horror,         // ホラー・オカルト
  SciFi,          // 未来・SF
  Retro           // レトロ
};

/**
 * @brief スカイボックステクスチャジェネレーター
 */
class SkyboxTextureGenerator {
public:
  SkyboxTextureGenerator() = default;
  ~SkyboxTextureGenerator() = default;

  /**
   * @brief ページ情報からテーマを判定してキューブマップを生成
   * @param device DirectX11デバイス
   * @param pageTitle ページタイトル
   * @param pageExtract ページ抜粋（カテゴリ判定用）
   * @param outSRV 生成されたキューブマップSRV（出力）
   * @return 成功ならtrue
   */
  bool
  GenerateCubemap(ID3D11Device *device, const std::string &pageTitle,
                  const std::string &pageExtract,
                  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &outSRV);

  /**
   * @brief 指定テーマでキューブマップを生成
   * @param device DirectX11デバイス
   * @param theme スカイボックステーマ
   * @param outSRV 生成されたキューブマップSRV（出力）
   * @return 成功ならtrue
   */
  bool GenerateCubemapFromTheme(
      ID3D11Device *device, SkyboxTheme theme,
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &outSRV);

  /**
   * @brief テクスチャを生成しPNGとして保存後、その画像からキューブマップを作成
   * @param device DirectX11デバイス
   * @param pageTitle ページタイトル
   * @param pageExtract ページ抜粋
   * @param baseFilePath 保存先のベースパス（_px.pngなどを付与して保存）
   * @param outSRV 生成されたキューブマップSRV
   * @return 成功ならtrue
   */
  bool GenerateCubemapToFiles(
      ID3D11Device *device, const std::string &pageTitle,
      const std::string &pageExtract, const std::wstring &baseFilePath,
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &outSRV);

  /**
   * @brief 既存のPNGファイル群からキューブマップを構築
   * @param device DirectX11デバイス
   * @param baseFilePath ベースパス（_px.pngなどを付与して読み込む）
   * @param outSRV 生成されたキューブマップSRV
   * @return 成功ならtrue
   */
  bool LoadCubemapFromFiles(
      ID3D11Device *device, const std::wstring &baseFilePath,
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &outSRV);

private:
  /**
   * @brief ページ情報からテーマを判定
   * @param pageTitle ページタイトル
   * @param pageExtract ページ抜粋
   * @return 判定されたテーマ
   */
  SkyboxTheme DetermineTheme(const std::string &pageTitle,
                             const std::string &pageExtract);

  /**
   * @brief テーマから色グラデーションを取得
   * @param theme テーマ
   * @param outTopColor 天頂色（出力）
   * @param outHorizonColor 地平線色（出力）
   * @param outBottomColor 天底色（出力）
   */
  void GetThemeColors(SkyboxTheme theme, DirectX::XMFLOAT3 &outTopColor,
                      DirectX::XMFLOAT3 &outHorizonColor,
                      DirectX::XMFLOAT3 &outBottomColor);

  /**
   * @brief 6面のテクスチャデータを生成
   * @param topColor 天頂色
   * @param horizonColor 地平線色
   * @param bottomColor 天底色
   * @param faceSize 各面のサイズ（ピクセル）
   * @param outData 生成されたテクスチャデータ（6面分）
   * @param theme スカイボックステーマ（エフェクト適用用）
   */
  void GenerateFaceData(const DirectX::XMFLOAT3 &topColor,
                        const DirectX::XMFLOAT3 &horizonColor,
                        const DirectX::XMFLOAT3 &bottomColor, int faceSize,
                        std::vector<std::vector<uint8_t>> &outData,
                        SkyboxTheme theme);

  /**
   * @brief キューブマップテクスチャとSRVを作成
   * @param device DirectX11デバイス
   * @param faceData 6面のテクスチャデータ
   * @param faceSize 各面のサイズ
   * @param outSRV 生成されたSRV（出力）
   * @return 成功ならtrue
   */
  bool CreateCubemapTexture(
      ID3D11Device *device, const std::vector<std::vector<uint8_t>> &faceData,
      int faceSize, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &outSRV);
};

} // namespace graphics
