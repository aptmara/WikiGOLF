/**
 * @file SkyboxTextureGeneratorProcedural.cpp
 * @brief SkyboxTextureGeneratorProcedural の実装
 */

#include "SkyboxTextureGenerator.h"
#include "SkyboxTextureGeneratorInternals.h"
#include <algorithm>
#include <cctype>
#include <combaseapi.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include <wincodec.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;


namespace graphics::skybox_detail {

/**
 * @brief テキストを小文字へ変換します。
*/
std::string ToLower(const std::string &text) {
  std::string result = text;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return result;
}

/**
 * @brief 指定キーワードの有無を判定します。
*/
bool ContainsKeyword(const std::string &text, const std::string &keyword) {
  return ToLower(text).find(ToLower(keyword)) != std::string::npos;
}

/**
 * @brief 複数キーワードのいずれかを判定します。
*/
bool ContainsAnyKeyword(const std::string &text,
                        const std::vector<std::string> &keywords) {
  for (const std::string &keyword : keywords) {
    if (ContainsKeyword(text, keyword)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 決定論的なノイズ値を計算します。
*/
float HashNoise(int x, int y, int z) {
  int value = x * 374761393 + y * 668265263 + z * 69069;
  value = (value ^ (value >> 13)) * 1274126177;
  return static_cast<float>((value ^ (value >> 16)) & 0x7FFFFFFF) /
         2147483647.0f;
}

/**
 * @brief 簡易パーリンノイズ風の関数
*/
float GenerateNoise(float x, float y, float z) {
  // 簡易ハッシュベースノイズ
  int xi = static_cast<int>(std::floor(x));
  int yi = static_cast<int>(std::floor(y));
  int zi = static_cast<int>(std::floor(z));

  float xf = x - xi;
  float yf = y - yi;
  float zf = z - zi;

  // スムーズステップ
  float u = xf * xf * (3.0f - 2.0f * xf);
  float v = yf * yf * (3.0f - 2.0f * yf);
  float w = zf * zf * (3.0f - 2.0f * zf);

  // 簡易ハッシュ
  auto hash = [](int a, int b, int c) -> float {
    int n = a * 374761393 + b * 668265263 + c;
    n = (n ^ (n >> 13)) * 1274126177;
    return static_cast<float>((n ^ (n >> 16)) & 0x7FFFFFFF) / 2147483647.0f;
  };

  // 補間
  float c000 = hash(xi, yi, zi);
  float c100 = hash(xi + 1, yi, zi);
  float c010 = hash(xi, yi + 1, zi);
  float c110 = hash(xi + 1, yi + 1, zi);
  float c001 = hash(xi, yi, zi + 1);
  float c101 = hash(xi + 1, yi, zi + 1);
  float c011 = hash(xi, yi + 1, zi + 1);
  float c111 = hash(xi + 1, yi + 1, zi + 1);

  float c00 = c000 * (1 - u) + c100 * u;
  float c10 = c010 * (1 - u) + c110 * u;
  float c01 = c001 * (1 - u) + c101 * u;
  float c11 = c011 * (1 - u) + c111 * u;

  float c0 = c00 * (1 - v) + c10 * v;
  float c1 = c01 * (1 - v) + c11 * v;

  return c0 * (1 - w) + c1 * w;
}

/**
 * @brief 星フィールド生成
*/
float GenerateStars(float x, float y, float z) {
  // 高周波ノイズで星を生成
  float star = GenerateNoise(x * 100.0f, y * 100.0f, z * 100.0f);

  // しきい値を超えたポイントのみ星として扱う
  if (star > 0.995f) {
    return 1.0f;
  }
  return 0.0f;
}

/**
 * @brief 雲パターン生成
*/
float GenerateClouds(float x, float y, float z) {
  // 複数のオクターブのノイズを重ねる
  float cloud = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;

  for (int i = 0; i < 3; ++i) {
    cloud +=
        GenerateNoise(x * frequency, y * frequency, z * frequency) * amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }

  // 0-1範囲にクランプ
  cloud = std::max(0.0f, std::min(1.0f, cloud));
  return cloud;
}

/**
 * @brief フラクタルブラウン運動（FBM）- 高品質ノイズ
*/
float GenerateFBM(float x, float y, float z, int octaves) {
  float value = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float maxValue = 0.0f;

  for (int i = 0; i < octaves; ++i) {
    value +=
        GenerateNoise(x * frequency, y * frequency, z * frequency) * amplitude;
    maxValue += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }

  return value / maxValue;
}

/**
 * @brief ワーリーノイズ - セルラーノイズで雲の塊を生成
*/
float GenerateWorleyNoise(float x, float y, float z) {
  int xi = static_cast<int>(std::floor(x));
  int yi = static_cast<int>(std::floor(y));
  int zi = static_cast<int>(std::floor(z));

  float minDist = 10.0f;

  // 周辺セルを探索
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dz = -1; dz <= 1; ++dz) {
        int cx = xi + dx;
        int cy = yi + dy;
        int cz = zi + dz;

        // セル内の点を生成
        auto hash = [](int a, int b, int c) -> float {
          int n = a * 374761393 + b * 668265263 + c;
          n = (n ^ (n >> 13)) * 1274126177;
          return static_cast<float>((n ^ (n >> 16)) & 0x7FFFFFFF) /
                 2147483647.0f;
        };

        float px = cx + hash(cx, cy, cz);
        float py = cy + hash(cx + 1, cy, cz);
        float pz = cz + hash(cx, cy + 1, cz);

        float dist = std::sqrt((x - px) * (x - px) + (y - py) * (y - py) +
                               (z - pz) * (z - pz));
        minDist = std::min(minDist, dist);
      }
    }
  }

  return 1.0f - std::min(minDist, 1.0f);
}

/**
 * @brief 銀河/天の川生成
*/
float GenerateGalaxy(float x, float y, float z) {
  // 特定の帯状領域に銀河を配置
  float band = std::abs(y - 0.2f);
  if (band > 0.3f)
    return 0.0f;

  float fbm = GenerateFBM(x * 3.0f, y * 3.0f, z * 3.0f, 4);
  float intensity = (1.0f - band / 0.3f) * fbm;
  return intensity * 0.3f;
}

/**
 * @brief 太陽/月の描画
*/
float GenerateSun(const DirectX::XMFLOAT3 &dir,
                  const DirectX::XMFLOAT3 &sunDir) {
  DirectX::XMVECTOR dirVec = DirectX::XMLoadFloat3(&dir);
  DirectX::XMVECTOR sunDirVec = DirectX::XMLoadFloat3(&sunDir);

  DirectX::XMVECTOR dotVec = DirectX::XMVector3Dot(dirVec, sunDirVec);
  float dot;
  DirectX::XMStoreFloat(&dot, dotVec);

  // 太陽のサイズと強度
  float sunSize = 0.015f;
  float dist = 1.0f - dot;

  if (dist < sunSize) {
    // 太陽の中心ほど明るい
    float intensity = 1.0f - (dist / sunSize);
    return intensity * intensity * 2.0f;
  }

  // 太陽のグロー（周辺の光）
  if (dist < sunSize * 4.0f) {
    float glowIntensity = 1.0f - (dist / (sunSize * 4.0f));
    return glowIntensity * glowIntensity * 0.5f;
  }

  return 0.0f;
}


ThemeParams GetThemeParams(SkyboxTheme theme) {
  ThemeParams p;

  switch (theme) {
  case SkyboxTheme::SpaceAstronomy:
    p.starMult = 3.0f;
    p.cloudMult = 0.2f;
    p.noiseMult = 1.2f;
    p.galaxyMult = 2.0f;
    p.gradientExponent = 0.9f;
    p.vignette = 0.12f;
    p.tint = {0.9f, 0.95f, 1.1f};
    p.accentStrength = 0.35f;
    p.accentFrequency = 8.0f;
    p.accentColor = {0.6f, 0.7f, 1.2f};
    break;
  case SkyboxTheme::Ocean:
    p.cloudMult = 1.0f;
    p.noiseMult = 1.5f;
    p.gradientExponent = 1.2f;
    p.fogStrength = 0.18f;
    p.fogExponent = 1.1f;
    p.tint = {0.9f, 1.0f, 1.1f};
    p.saturation = 1.1f;
    break;
  case SkyboxTheme::Volcano:
    p.starMult = 0.2f;
    p.cloudMult = 1.5f;
    p.noiseMult = 2.0f;
    p.gradientExponent = 0.8f;
    p.fogStrength = 0.25f;
    p.tint = {1.1f, 0.9f, 0.8f};
    p.accentStrength = 0.3f;
    p.accentFrequency = 9.0f;
    p.accentColor = {1.2f, 0.6f, 0.2f};
    break;
  case SkyboxTheme::Polar:
    p.cloudMult = 1.2f;
    p.starMult = 1.5f;
    p.galaxyMult = 1.5f;
    p.gradientExponent = 1.4f;
    p.fogStrength = 0.2f;
    p.tint = {0.9f, 1.05f, 1.1f};
    p.accentStrength = 0.25f;
    p.accentFrequency = 6.5f;
    p.accentColor = {0.6f, 0.95f, 1.2f};
    break;
  case SkyboxTheme::Desert:
    p.cloudMult = 1.4f;
    p.noiseMult = 1.3f;
    p.gradientExponent = 1.3f;
    p.fogStrength = 0.22f;
    p.tint = {1.05f, 1.0f, 0.9f};
    p.saturation = 1.08f;
    break;
  case SkyboxTheme::Forest:
    p.cloudMult = 0.8f;
    p.noiseMult = 1.5f;
    p.fogStrength = 0.18f;
    p.fogExponent = 1.3f;
    p.tint = {0.9f, 1.05f, 0.9f};
    break;
  case SkyboxTheme::Mountain:
    p.cloudMult = 1.4f;
    p.noiseMult = 1.2f;
    p.gradientExponent = 1.4f;
    p.tint = {0.95f, 1.0f, 1.05f};
    break;
  case SkyboxTheme::Horror:
    p.starMult = 1.5f;
    p.cloudMult = 0.6f;
    p.noiseMult = 2.0f;
    p.galaxyMult = 0.5f;
    p.vignette = 0.2f;
    p.contrast = 1.12f;
    p.saturation = 0.85f;
    p.tint = {0.9f, 0.8f, 1.0f};
    p.accentStrength = 0.22f;
    p.accentFrequency = 7.0f;
    p.accentColor = {0.6f, 0.2f, 0.7f};
    break;
  case SkyboxTheme::Fantasy:
    p.starMult = 1.5f;
    p.cloudMult = 1.1f;
    p.noiseMult = 1.4f;
    p.tint = {1.05f, 1.0f, 1.05f};
    p.saturation = 1.2f;
    p.accentStrength = 0.3f;
    p.accentColor = {0.9f, 0.6f, 1.2f};
    break;
  case SkyboxTheme::SciFi:
    p.starMult = 1.0f;
    p.cloudMult = 0.5f;
    p.noiseMult = 1.8f;
    p.galaxyMult = 0.8f;
    p.contrast = 1.15f;
    p.saturation = 1.2f;
    p.vignette = 0.08f;
    p.tint = {0.8f, 1.05f, 1.2f};
    p.accentStrength = 0.35f;
    p.accentFrequency = 12.0f;
    p.accentColor = {0.2f, 1.0f, 1.5f};
    break;
  case SkyboxTheme::War:
    p.starMult = 0.3f;
    p.cloudMult = 2.0f;
    p.noiseMult = 2.5f;
    p.galaxyMult = 0.0f;
    p.vignette = 0.15f;
    p.contrast = 1.08f;
    p.saturation = 0.9f;
    p.fogStrength = 0.3f;
    p.tint = {0.95f, 0.9f, 0.9f};
    break;
  case SkyboxTheme::Urban:
    p.cloudMult = 1.2f;
    p.noiseMult = 1.8f;
    p.fogStrength = 0.25f;
    p.fogExponent = 1.0f;
    p.saturation = 0.95f;
    p.tint = {0.95f, 1.0f, 1.05f};
    break;
  case SkyboxTheme::Sunset:
    p.starMult = 0.6f;
    p.cloudMult = 1.5f;
    p.noiseMult = 1.5f;
    p.gradientExponent = 0.8f;
    p.fogStrength = 0.2f;
    p.tint = {1.08f, 1.0f, 0.95f};
    p.saturation = 1.15f;
    p.accentStrength = 0.25f;
    p.accentColor = {1.2f, 0.5f, 0.3f};
    break;
  case SkyboxTheme::Sports:
    p.cloudMult = 1.6f;
    p.noiseMult = 1.3f;
    p.saturation = 1.1f;
    p.tint = {1.05f, 1.05f, 1.05f};
    break;
  case SkyboxTheme::Art:
    p.cloudMult = 1.1f;
    p.noiseMult = 1.3f;
    p.saturation = 1.2f;
    p.tint = {1.05f, 1.0f, 1.05f};
    p.accentStrength = 0.18f;
    p.accentColor = {1.1f, 0.7f, 1.0f};
    break;
  case SkyboxTheme::Music:
    p.starMult = 1.2f;
    p.cloudMult = 0.9f;
    p.noiseMult = 1.3f;
    p.saturation = 1.1f;
    p.vignette = 0.08f;
    p.accentStrength = 0.2f;
    p.accentFrequency = 10.0f;
    p.accentColor = {0.8f, 0.9f, 1.2f};
    break;
  case SkyboxTheme::Literature:
    p.cloudMult = 1.0f;
    p.noiseMult = 1.2f;
    p.saturation = 0.95f;
    p.tint = {1.05f, 1.05f, 0.95f};
    break;
  case SkyboxTheme::Medical:
    p.cloudMult = 0.9f;
    p.noiseMult = 1.2f;
    p.saturation = 0.95f;
    p.tint = {0.95f, 1.05f, 1.05f};
    p.vignette = 0.05f;
    break;
  case SkyboxTheme::Food:
    p.cloudMult = 1.3f;
    p.noiseMult = 1.4f;
    p.saturation = 1.15f;
    p.tint = {1.08f, 1.0f, 0.95f};
    break;
  case SkyboxTheme::Religion:
    p.starMult = 1.4f;
    p.cloudMult = 1.0f;
    p.noiseMult = 1.2f;
    p.galaxyMult = 1.2f;
    p.tint = {1.05f, 1.0f, 1.05f};
    p.accentStrength = 0.18f;
    p.accentColor = {1.1f, 1.0f, 0.8f};
    break;
  case SkyboxTheme::Retro:
    p.cloudMult = 0.9f;
    p.noiseMult = 1.1f;
    p.saturation = 0.9f;
    p.vignette = 0.12f;
    p.tint = {1.05f, 1.02f, 0.95f};
    break;
  case SkyboxTheme::GolfCourseClear: // 晴天ゴルフコース：大きな白雲・爽やかな青空
    p.cloudMult      = 1.8f;    // 大きな白雲をしっかり出す
    p.noiseMult      = 1.2f;    // 空のテクスチャ感
    p.saturation     = 1.18f;   // 鮮やかな青空
    p.fogStrength    = 0.12f;   // 地平線の大気霞
    p.fogExponent    = 0.7f;    // 霞が広範囲に広がる
    p.gradientExponent = 1.3f;  // 天頂の青が強く出る
    p.contrast       = 1.08f;   // 若干強め
    p.sunSize        = 0.022f;  // やや大きい太陽
    p.tint           = {0.96f, 0.98f, 1.02f}; // わずかにクール寄り
    p.accentStrength = 0.20f;   // 雲の縁の白い輝き
    p.accentFrequency = 3.0f;   // 大きな波長（雲スケール）
    p.accentColor    = {1.0f, 1.0f, 1.0f}; // 純白
    p.vignette       = 0.04f;
    break;
  default:
    break;
  }

  return p;
}

} // namespace graphics::skybox_detail
