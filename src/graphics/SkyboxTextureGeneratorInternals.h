#pragma once
/**
 * @file SkyboxTextureGeneratorInternals.h
 * @brief スカイボックス生成で共有する内部処理の宣言
 */

#include "SkyboxTextureGenerator.h"
#include <wincodec.h>
#include <vector>

namespace graphics::skybox_detail {

inline constexpr int kDefaultFaceSize = 512;

/**
 * @brief テーマごとのプロシージャル生成パラメータ
 */
struct ThemeParams {
  float starMult = 1.0f;
  float cloudMult = 1.0f;
  float noiseMult = 1.0f;
  float galaxyMult = 1.0f;
  float sunSize = 0.015f;
  float fogStrength = 0.1f;
  float fogExponent = 1.6f;
  float gradientExponent = 1.0f;
  float contrast = 1.05f;
  float saturation = 1.05f;
  float vignette = 0.06f;
  float accentStrength = 0.0f;
  float accentFrequency = 4.0f;
  float nebulaStrength = 0.0f;
  float ribbonStrength = 0.0f;
  float ribbonFrequency = 3.0f;
  float ribbonSharpness = 6.0f;
  DirectX::XMFLOAT3 tint = {1.0f, 1.0f, 1.0f};
  DirectX::XMFLOAT3 accentColor = {1.0f, 1.0f, 1.0f};
  DirectX::XMFLOAT3 nebulaColor = {0.6f, 0.4f, 0.9f};
  DirectX::XMFLOAT3 ribbonColor = {0.6f, 0.9f, 1.2f};
};

/**
 * @brief テキストを小文字へ変換します。
 */
std::string ToLower(const std::string &text);

/**
 * @brief 指定キーワードの有無を判定します。
 */
bool ContainsKeyword(const std::string &text, const std::string &keyword);

/**
 * @brief 複数キーワードのいずれかを判定します。
 */
bool ContainsAnyKeyword(const std::string &text,
                        const std::vector<std::string> &keywords);

/**
 * @brief 決定論的なノイズ値を計算します。
 */
float HashNoise(int x, int y, int z);

/**
 * @brief WIC画像ファクトリを取得します。
 */
Microsoft::WRL::ComPtr<IWICImagingFactory> GetWicFactory();

/**
 * @brief RGBA面データをPNGへ保存します。
 */
bool SaveFaceToFile(const std::vector<uint8_t> &data, int faceSize,
                    const std::wstring &path);

/**
 * @brief PNGをRGBA面データへ読み込みます。
 */
bool LoadFaceFromFile(const std::wstring &path, int targetSize,
                      std::vector<uint8_t> &outData);

float GenerateNoise(float x, float y, float z);
float GenerateStars(float x, float y, float z);
float GenerateClouds(float x, float y, float z);
float GenerateFBM(float x, float y, float z, int octaves = 6);
float GenerateWorleyNoise(float x, float y, float z);
float GenerateGalaxy(float x, float y, float z);
float GenerateSun(const DirectX::XMFLOAT3 &dir,
                  const DirectX::XMFLOAT3 &sunDir);

/**
 * @brief テーマのプロシージャルパラメータを取得します。
 */
ThemeParams GetThemeParams(SkyboxTheme theme);

} // namespace graphics::skybox_detail

