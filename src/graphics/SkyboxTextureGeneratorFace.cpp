/**
 * @file SkyboxTextureGeneratorFace.cpp
 * @brief SkyboxTextureGeneratorFace の実装
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


namespace graphics {

void SkyboxTextureGenerator::GenerateFaceData(
    const XMFLOAT3 &topColor, const XMFLOAT3 &horizonColor,
    const XMFLOAT3 &bottomColor, int faceSize,
    std::vector<std::vector<uint8_t>> &outData, SkyboxTheme theme,
    uint8_t faceMask) {

  outData.resize(6); // 6面

  std::srand(12345);

  skybox_detail::ThemeParams params = skybox_detail::GetThemeParams(theme);

  for (int face = 0; face < 6; ++face) {
    if (!(faceMask & (1 << face))) {
      continue;
    }
    outData[face].resize(faceSize * faceSize * 4); // RGBA形式

    for (int y = 0; y < faceSize; ++y) {
      for (int x = 0; x < faceSize; ++x) {
        // UV座標（-1 ~ 1）
        float u = (x / (float)(faceSize - 1)) * 2.0f - 1.0f;
        float v = (y / (float)(faceSize - 1)) * 2.0f - 1.0f;

        // キューブマップの3D方向を計算
        XMFLOAT3 dir;
        switch (face) {
        case 0: // +X
          dir = {1.0f, -v, -u};
          break;
        case 1: // -X
          dir = {-1.0f, -v, u};
          break;
        case 2: // +Y（天頂）
          dir = {u, 1.0f, v};
          break;
        case 3: // -Y（天底）
          dir = {u, -1.0f, -v};
          break;
        case 4: // +Z
          dir = {u, -v, 1.0f};
          break;
        case 5: // -Z
          dir = {-u, -v, -1.0f};
          break;
        }

        // 正規化
        XMVECTOR dirVec = XMLoadFloat3(&dir);
        dirVec = XMVector3Normalize(dirVec);
        XMStoreFloat3(&dir, dirVec);

        float yFactor = dir.y;

        XMFLOAT3 color;
        if (yFactor > 0.0f) {
          float t = std::pow(yFactor, params.gradientExponent);
          color.x = horizonColor.x + (topColor.x - horizonColor.x) * t;
          color.y = horizonColor.y + (topColor.y - horizonColor.y) * t;
          color.z = horizonColor.z + (topColor.z - horizonColor.z) * t;
        } else {
          float t = std::pow(-yFactor, params.gradientExponent);
          color.x = horizonColor.x + (bottomColor.x - horizonColor.x) * t;
          color.y = horizonColor.y + (bottomColor.y - horizonColor.y) * t;
          color.z = horizonColor.z + (bottomColor.z - horizonColor.z) * t;
        }

        // === プロシージャル要素 ===

        XMFLOAT3 sunDir = {0.7f, 0.5f, 0.3f};
        if (theme == SkyboxTheme::GolfCourseClear) {
          // 右後方から高い位置
          sunDir = {0.6f, 0.75f, 0.4f};
        }

        float noise = skybox_detail::GenerateNoise(dir.x * 5.0f, dir.y * 5.0f, dir.z * 5.0f) *
                      params.noiseMult;
        float starIntensity =
            skybox_detail::GenerateStars(dir.x, dir.y, dir.z) * params.starMult;
        float galaxyIntensity =
            skybox_detail::GenerateGalaxy(dir.x, dir.y, dir.z) * params.galaxyMult;
        float sunIntensity =
            skybox_detail::GenerateSun(dir, sunDir) * (params.sunSize / 0.015f);

        float fbmCloud =
            skybox_detail::GenerateFBM(dir.x * 2.0f, dir.y * 2.0f, dir.z * 2.0f, 5);
        float worleyCloud =
            skybox_detail::GenerateWorleyNoise(dir.x * 3.0f, dir.y * 3.0f, dir.z * 3.0f);
        float cloudPattern =
            (fbmCloud * 0.6f + worleyCloud * 0.4f) * params.cloudMult;

        float detail =
            skybox_detail::GenerateNoise(dir.x * 10.0f, dir.y * 10.0f, dir.z * 10.0f) * 0.1f;
        cloudPattern += detail;
        cloudPattern = std::max(0.0f, std::min(1.0f, cloudPattern));

        // === エフェクト適用 ===

        float noiseInfluence = 0.05f * params.noiseMult;
        color.x += (noise - 0.5f) * noiseInfluence;
        color.y += (noise - 0.5f) * noiseInfluence;
        color.z += (noise - 0.5f) * noiseInfluence;

        if (sunIntensity > 0.0f && params.cloudMult > 0.0f) {
          color.x += sunIntensity * 0.9f;
          color.y += sunIntensity * 0.8f;
          color.z += sunIntensity * 0.5f;
        }

        float cloudYMax = 0.8f;
        if (theme == SkyboxTheme::GolfCourseClear) {
          cloudYMax = 0.95f;
        }
        if (yFactor > -0.2f && yFactor < cloudYMax && params.cloudMult > 0.0f) {
          float currentCloudPattern = cloudPattern;
          if (theme == SkyboxTheme::GolfCourseClear) {
              // 積雲: worley ノイズを強めて輪郭をくっきりさせる
              currentCloudPattern = std::pow(cloudPattern, 2.0f);
          }
          float cloudBase = currentCloudPattern * 0.28f * params.cloudMult;
          float lightingFactor = std::max(0.0f, -dir.x * 0.5f + 0.5f);
          float cloudHighlight = currentCloudPattern * currentCloudPattern * 0.3f *
                                 lightingFactor * params.cloudMult;

          color.x += cloudBase + cloudHighlight;
          color.y += cloudBase + cloudHighlight;
          color.z += cloudBase + cloudHighlight;
        }

        float brightness = (color.x + color.y + color.z) / 3.0f;
        if (brightness < 0.3f && starIntensity > 0.9f &&
            params.starMult > 0.0f) {
          float starBoost = (starIntensity - 0.9f) * 10.0f * params.starMult;
          color.x += starBoost * (0.8f + noise * 0.2f);
          color.y += starBoost * (0.8f + noise * 0.15f);
          color.z += starBoost * (1.0f + noise * 0.1f);
        }

        if (brightness < 0.4f && galaxyIntensity > 0.0f &&
            params.starMult > 1.5f) {
          color.x += galaxyIntensity * 0.5f * params.galaxyMult;
          color.y += galaxyIntensity * 0.4f * params.galaxyMult;
          color.z += galaxyIntensity * 0.6f * params.galaxyMult;
        }

        if (params.accentStrength > 0.0f) {
          float accentBase =
              0.5f * (std::sin((dir.x + dir.z) * params.accentFrequency +
                               dir.y * params.accentFrequency * 0.5f) +
                      1.0f);
          float accent = std::pow(accentBase, 4.0f) * params.accentStrength;
          color.x += accent * params.accentColor.x;
          color.y += accent * params.accentColor.y;
          color.z += accent * params.accentColor.z;
        }

        // ネビュラ（拡散した帯状の光）
        if (params.nebulaStrength > 0.0f) {
          float nebula =
              skybox_detail::GenerateFBM(dir.x * 4.0f, dir.y * 2.0f, dir.z * 4.0f, 6);
          nebula = std::pow(nebula, 3.0f) * params.nebulaStrength;
          color.x += nebula * params.nebulaColor.x;
          color.y += nebula * params.nebulaColor.y;
          color.z += nebula * params.nebulaColor.z;
        }

        // リボン状の光（オーロラ/ネオン帯）
        if (params.ribbonStrength > 0.0f) {
          float ribbonWave = std::sin(dir.x * params.ribbonFrequency) *
                             std::cos(dir.z * params.ribbonFrequency * 0.7f);
          float ribbon =
              std::pow(std::abs(ribbonWave), params.ribbonSharpness) *
              params.ribbonStrength;
          // 偏りをy方向で強調
          ribbon *= 0.5f + 0.5f * (1.0f - std::abs(dir.y));
          color.x += ribbon * params.ribbonColor.x;
          color.y += ribbon * params.ribbonColor.y;
          color.z += ribbon * params.ribbonColor.z;
        }

        float fog = std::pow(1.0f - std::abs(dir.y), params.fogExponent) *
                    params.fogStrength;
        color.x = color.x * (1.0f - fog) + horizonColor.x * fog;
        color.y = color.y * (1.0f - fog) + horizonColor.y * fog;
        color.z = color.z * (1.0f - fog) + horizonColor.z * fog;

        float lum = color.x * 0.299f + color.y * 0.587f + color.z * 0.114f;
        color.x = lum + (color.x - lum) * params.saturation;
        color.y = lum + (color.y - lum) * params.saturation;
        color.z = lum + (color.z - lum) * params.saturation;

        color.x = (color.x - 0.5f) * params.contrast + 0.5f;
        color.y = (color.y - 0.5f) * params.contrast + 0.5f;
        color.z = (color.z - 0.5f) * params.contrast + 0.5f;

        color.x *= params.tint.x;
        color.y *= params.tint.y;
        color.z *= params.tint.z;

        float radius = std::sqrt(u * u + v * v);
        float vignette =
            std::pow(std::min(1.0f, radius), 2.2f) * params.vignette;
        color.x *= (1.0f - vignette);
        color.y *= (1.0f - vignette);
        color.z *= (1.0f - vignette);

        float dither = skybox_detail::HashNoise(x, y, face) - 0.5f;
        color.x = std::max(0.0f, std::min(1.0f, color.x + dither * 0.003f));
        color.y = std::max(0.0f, std::min(1.0f, color.y + dither * 0.003f));
        color.z = std::max(0.0f, std::min(1.0f, color.z + dither * 0.003f));

        // RGB値を0-255に変換
        int idx = (y * faceSize + x) * 4;
        outData[face][idx + 0] = static_cast<uint8_t>(color.x * 255.0f);
        outData[face][idx + 1] = static_cast<uint8_t>(color.y * 255.0f);
        outData[face][idx + 2] = static_cast<uint8_t>(color.z * 255.0f);
        outData[face][idx + 3] = 255; // アルファ値
      }
    }
  }
}

} // namespace graphics
