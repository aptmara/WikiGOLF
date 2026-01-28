#include "SkyboxTextureGenerator.h"
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

namespace {

constexpr int kDefaultFaceSize = 512;
const wchar_t *kFaceSuffixes[6] = {L"_px.png", L"_nx.png", L"_py.png",
                                   L"_ny.png", L"_pz.png", L"_nz.png"};

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
  XMFLOAT3 tint = {1.0f, 1.0f, 1.0f};
  XMFLOAT3 accentColor = {1.0f, 1.0f, 1.0f};
  XMFLOAT3 nebulaColor = {0.6f, 0.4f, 0.9f};
  XMFLOAT3 ribbonColor = {0.6f, 0.9f, 1.2f};
};

/**
 * @brief テキストを小文字に変換
 */
std::string ToLower(const std::string &text) {
  std::string result = text;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

/**
 * @brief テキストに指定キーワードが含まれるかチェック
 */
bool ContainsKeyword(const std::string &text, const std::string &keyword) {
  return ToLower(text).find(ToLower(keyword)) != std::string::npos;
}

/**
 * @brief 複数キーワードのいずれかが含まれるかチェック
 */
bool ContainsAnyKeyword(const std::string &text,
                        const std::vector<std::string> &keywords) {
  for (const auto &keyword : keywords) {
    if (ContainsKeyword(text, keyword)) {
      return true;
    }
  }
  return false;
}

float HashNoise(int x, int y, int z) {
  int n = x * 374761393 + y * 668265263 + z * 69069;
  n = (n ^ (n >> 13)) * 1274126177;
  return static_cast<float>((n ^ (n >> 16)) & 0x7FFFFFFF) / 2147483647.0f;
}

Microsoft::WRL::ComPtr<IWICImagingFactory> GetWicFactory() {
  static std::once_flag flag;
  static Microsoft::WRL::ComPtr<IWICImagingFactory> factory;

  std::call_once(flag, []() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory));
  });

  return factory;
}

bool SaveFaceToFile(const std::vector<uint8_t> &data, int faceSize,
                    const std::wstring &path) {
  auto factory = GetWicFactory();
  if (!factory) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  HRESULT hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return false;
  }

  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    return false;
  }

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  hr = encoder->CreateNewFrame(&frame, nullptr);
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->Initialize(nullptr);
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->SetSize(faceSize, faceSize);
  if (FAILED(hr)) {
    return false;
  }

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
  hr = frame->SetPixelFormat(&format);
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->WritePixels(faceSize, faceSize * 4,
                          static_cast<UINT>(data.size()),
                          reinterpret_cast<BYTE *>(
                              const_cast<uint8_t *>(data.data())));
  if (FAILED(hr)) {
    return false;
  }

  hr = frame->Commit();
  if (FAILED(hr)) {
    return false;
  }

  hr = encoder->Commit();
  return SUCCEEDED(hr);
}

bool LoadFaceFromFile(const std::wstring &path, int expectedSize,
                      std::vector<uint8_t> &outData) {
  auto factory = GetWicFactory();
  if (!factory) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  HRESULT hr =
      factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                         WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    return false;
  }

  UINT width = 0;
  UINT height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr)) {
    return false;
  }

  if (width != static_cast<UINT>(expectedSize) ||
      height != static_cast<UINT>(expectedSize)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    return false;
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return false;
  }

  outData.resize(expectedSize * expectedSize * 4);
  hr = converter->CopyPixels(
      nullptr, expectedSize * 4, static_cast<UINT>(outData.size()),
      outData.data());
  return SUCCEEDED(hr);
}

} // namespace

bool SkyboxTextureGenerator::GenerateCubemap(
    ID3D11Device *device, const std::string &pageTitle,
    const std::string &pageExtract, ComPtr<ID3D11ShaderResourceView> &outSRV) {

  SkyboxTheme theme = DetermineTheme(pageTitle, pageExtract);
  return GenerateCubemapFromTheme(device, theme, outSRV);
}

bool SkyboxTextureGenerator::GenerateCubemapFromTheme(
    ID3D11Device *device, SkyboxTheme theme,
    ComPtr<ID3D11ShaderResourceView> &outSRV) {

  XMFLOAT3 topColor, horizonColor, bottomColor;
  GetThemeColors(theme, topColor, horizonColor, bottomColor);

  const int faceSize = kDefaultFaceSize; // 各面512x512
  std::vector<std::vector<uint8_t>> faceData;
  GenerateFaceData(topColor, horizonColor, bottomColor, faceSize, faceData,
                   theme);

  return CreateCubemapTexture(device, faceData, faceSize, outSRV);
}

bool SkyboxTextureGenerator::GenerateCubemapToFiles(
    ID3D11Device *device, const std::string &pageTitle,
    const std::string &pageExtract, const std::wstring &baseFilePath,
    ComPtr<ID3D11ShaderResourceView> &outSRV) {

  SkyboxTheme theme = DetermineTheme(pageTitle, pageExtract);

  XMFLOAT3 topColor, horizonColor, bottomColor;
  GetThemeColors(theme, topColor, horizonColor, bottomColor);

  std::vector<std::vector<uint8_t>> faceData;
  GenerateFaceData(topColor, horizonColor, bottomColor, kDefaultFaceSize,
                   faceData, theme);

  std::filesystem::path basePath(baseFilePath);
  if (!basePath.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(basePath.parent_path(), ec);
  }

  bool savedAll = true;
  for (int i = 0; i < 6; ++i) {
    std::filesystem::path facePath = basePath;
    facePath += kFaceSuffixes[i];
    if (!SaveFaceToFile(faceData[i], kDefaultFaceSize, facePath.wstring())) {
      savedAll = false;
    }
  }

  if (savedAll) {
    return LoadCubemapFromFiles(device, baseFilePath, outSRV);
  }

  // 保存に失敗した場合は生成データから直接作成する
  return CreateCubemapTexture(device, faceData, kDefaultFaceSize, outSRV);
}

bool SkyboxTextureGenerator::LoadCubemapFromFiles(
    ID3D11Device *device, const std::wstring &baseFilePath,
    ComPtr<ID3D11ShaderResourceView> &outSRV) {

  std::vector<std::vector<uint8_t>> faceData(6);
  for (int i = 0; i < 6; ++i) {
    std::filesystem::path facePath(baseFilePath);
    facePath += kFaceSuffixes[i];
    if (!std::filesystem::exists(facePath)) {
      return false;
    }

    if (!LoadFaceFromFile(facePath.wstring(), kDefaultFaceSize,
                          faceData[i])) {
      return false;
    }
  }

  return CreateCubemapTexture(device, faceData, kDefaultFaceSize, outSRV);
}

// === プロシージャル生成ヘルパー関数 ===

namespace {

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
float GenerateFBM(float x, float y, float z, int octaves = 6) {
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

} // namespace

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
  default:
    break;
  }

  return p;
}

SkyboxTheme
SkyboxTextureGenerator::DetermineTheme(const std::string &pageTitle,
                                       const std::string &pageExtract) {

  std::string combined = pageTitle + " " + pageExtract;

  // 優先度順にテーマ判定（より具体的なものから）

  // 宇宙・天文
  if (ContainsAnyKeyword(combined, {"宇宙", "天文", "惑星", "星", "銀河", "月",
                                    "太陽系", "ブラックホール", "彗星", "space",
                                    "astronomy", "planet", "galaxy"})) {
    return SkyboxTheme::SpaceAstronomy;
  }

  // 海洋・水中
  if (ContainsAnyKeyword(combined, {"海", "海洋", "水中", "深海", "サンゴ礁",
                                    "イルカ", "クジラ", "魚", "ocean", "sea",
                                    "underwater", "marine"})) {
    return SkyboxTheme::Ocean;
  }

  // 火山
  if (ContainsAnyKeyword(combined, {"火山", "噴火", "溶岩", "マグマ", "volcano",
                                    "lava", "eruption"})) {
    return SkyboxTheme::Volcano;
  }

  // 極地・雪
  if (ContainsAnyKeyword(combined, {"極地", "南極", "北極", "氷河", "雪",
                                    "オーロラ", "polar", "arctic", "antarctica",
                                    "glacier", "snow"})) {
    return SkyboxTheme::Polar;
  }

  // 砂漠
  if (ContainsAnyKeyword(combined, {"砂漠", "サハラ", "乾燥", "オアシス",
                                    "desert", "sahara", "dune"})) {
    return SkyboxTheme::Desert;
  }

  // 森林・ジャングル
  if (ContainsAnyKeyword(combined,
                         {"森", "森林", "ジャングル", "熱帯雨林", "木",
                          "forest", "jungle", "rainforest", "woods"})) {
    return SkyboxTheme::Forest;
  }

  // 山岳・登山
  if (ContainsAnyKeyword(combined,
                         {"山", "登山", "高山", "ヒマラヤ", "アルプス",
                          "mountain", "climbing", "peak", "summit"})) {
    return SkyboxTheme::Mountain;
  }

  // ホラー・オカルト
  if (ContainsAnyKeyword(combined,
                         {"ホラー", "幽霊", "お化け", "オカルト", "怪談",
                          "呪い", "horror", "ghost", "haunted", "occult"})) {
    return SkyboxTheme::Horror;
  }

  // ファンタジー
  if (ContainsAnyKeyword(combined,
                         {"魔法", "魔術", "ドラゴン", "エルフ", "ファンタジー",
                          "fantasy", "magic", "wizard", "dragon"})) {
    return SkyboxTheme::Fantasy;
  }

  // SF・未来
  if (ContainsAnyKeyword(combined,
                         {"SF", "サイエンスフィクション", "未来", "ロボット",
                          "AI", "サイバー", "sci-fi", "science fiction",
                          "cyberpunk", "futuristic"})) {
    return SkyboxTheme::SciFi;
  }

  // 戦争・軍事
  if (ContainsAnyKeyword(combined,
                         {"戦争", "軍事", "兵器", "戦闘", "軍隊", "war",
                          "military", "battle", "weapon", "soldier"})) {
    return SkyboxTheme::War;
  }

  // 中世・城
  if (ContainsAnyKeyword(combined,
                         {"中世", "城", "騎士", "王国", "貴族", "medieval",
                          "castle", "knight", "kingdom"})) {
    return SkyboxTheme::Medieval;
  }

  // 歴史・古代
  if (ContainsAnyKeyword(combined,
                         {"歴史", "古代", "遺跡", "文明", "考古学", "history",
                          "ancient", "civilization", "archaeology", "ruins"})) {
    return SkyboxTheme::HistoryAncient;
  }

  // 宗教・神話
  if (ContainsAnyKeyword(combined, {"宗教", "神", "仏教", "キリスト教", "神話",
                                    "寺", "教会", "religion", "god",
                                    "mythology", "temple", "shrine"})) {
    return SkyboxTheme::Religion;
  }

  // 都市・建築
  if (ContainsAnyKeyword(combined,
                         {"都市", "建築", "ビル", "摩天楼", "都会", "city",
                          "urban", "building", "architecture", "skyscraper"})) {
    return SkyboxTheme::Urban;
  }

  // 医療・生物
  if (ContainsAnyKeyword(combined,
                         {"医療", "医学", "病院", "生物", "細胞", "DNA",
                          "medical", "medicine", "biology", "hospital"})) {
    return SkyboxTheme::Medical;
  }

  // 科学・技術
  if (ContainsAnyKeyword(combined, {"科学", "技術", "物理", "化学", "工学",
                                    "science", "technology", "physics",
                                    "chemistry", "engineering"})) {
    return SkyboxTheme::ScienceTech;
  }

  // 音楽
  if (ContainsAnyKeyword(combined,
                         {"音楽", "楽器", "演奏", "コンサート", "オーケストラ",
                          "music", "instrument", "concert", "orchestra"})) {
    return SkyboxTheme::Music;
  }

  // 芸術・美術
  if (ContainsAnyKeyword(combined,
                         {"芸術", "美術", "絵画", "彫刻", "芸術家", "art",
                          "painting", "sculpture", "artist", "gallery"})) {
    return SkyboxTheme::Art;
  }

  // 文学
  if (ContainsAnyKeyword(combined,
                         {"文学", "小説", "詩", "作家", "文芸", "literature",
                          "novel", "poetry", "writer", "author"})) {
    return SkyboxTheme::Literature;
  }

  // 食品・料理
  if (ContainsAnyKeyword(combined, {"料理", "食品", "レシピ", "グルメ",
                                    "レストラン", "food", "cooking", "cuisine",
                                    "recipe", "restaurant"})) {
    return SkyboxTheme::Food;
  }

  // スポーツ
  if (ContainsAnyKeyword(combined, {"スポーツ", "競技", "選手", "オリンピック",
                                    "野球", "サッカー", "sports", "athlete",
                                    "olympics", "game"})) {
    return SkyboxTheme::Sports;
  }

  // 夕暮れ・夜
  if (ContainsAnyKeyword(combined, {"夜", "夕暮れ", "夕焼け", "黄昏", "night",
                                    "sunset", "dusk", "twilight"})) {
    return SkyboxTheme::Sunset;
  }

  // レトロ
  if (ContainsAnyKeyword(combined, {"レトロ", "昭和", "ヴィンテージ", "古い",
                                    "retro", "vintage", "classic", "old"})) {
    return SkyboxTheme::Retro;
  }

  // デフォルト（青空）
  return SkyboxTheme::Default;
}

void SkyboxTextureGenerator::GetThemeColors(SkyboxTheme theme,
                                            XMFLOAT3 &outTopColor,
                                            XMFLOAT3 &outHorizonColor,
                                            XMFLOAT3 &outBottomColor) {
  // 床の文字を見やすくするため、全体的に彩度と明度を抑えめに設定
  // RGB値を0-1の範囲で指定

  switch (theme) {
  case SkyboxTheme::Default: // 青空
    outTopColor = {0.4f, 0.6f, 0.9f};
    outHorizonColor = {0.7f, 0.8f, 0.95f};
    outBottomColor = {0.6f, 0.7f, 0.85f};
    break;

  case SkyboxTheme::HistoryAncient: // セピア調
    outTopColor = {0.55f, 0.45f, 0.35f};
    outHorizonColor = {0.65f, 0.55f, 0.45f};
    outBottomColor = {0.5f, 0.4f, 0.3f};
    break;

  case SkyboxTheme::Medieval: // 灰色・重厚
    outTopColor = {0.5f, 0.5f, 0.55f};
    outHorizonColor = {0.6f, 0.6f, 0.65f};
    outBottomColor = {0.45f, 0.45f, 0.5f};
    break;

  case SkyboxTheme::ScienceTech: // サイバーブルー
    outTopColor = {0.2f, 0.4f, 0.7f};
    outHorizonColor = {0.3f, 0.5f, 0.8f};
    outBottomColor = {0.15f, 0.35f, 0.65f};
    break;

  case SkyboxTheme::SpaceAstronomy: // 深い紫→黒
    outTopColor = {0.1f, 0.05f, 0.2f};
    outHorizonColor = {0.2f, 0.1f, 0.3f};
    outBottomColor = {0.05f, 0.03f, 0.15f};
    break;

  case SkyboxTheme::Ocean: // 深海ブルー
    outTopColor = {0.1f, 0.3f, 0.5f};
    outHorizonColor = {0.2f, 0.5f, 0.7f};
    outBottomColor = {0.05f, 0.2f, 0.4f};
    break;

  case SkyboxTheme::Mountain: // 高山の澄んだ空
    outTopColor = {0.5f, 0.7f, 0.95f};
    outHorizonColor = {0.75f, 0.85f, 0.98f};
    outBottomColor = {0.65f, 0.75f, 0.9f};
    break;

  case SkyboxTheme::Forest: // 緑がかった霧
    outTopColor = {0.4f, 0.5f, 0.45f};
    outHorizonColor = {0.5f, 0.6f, 0.55f};
    outBottomColor = {0.35f, 0.45f, 0.4f};
    break;

  case SkyboxTheme::Desert: // 砂色→オレンジ
    outTopColor = {0.7f, 0.55f, 0.3f};
    outHorizonColor = {0.8f, 0.65f, 0.4f};
    outBottomColor = {0.65f, 0.5f, 0.25f};
    break;

  case SkyboxTheme::Polar: // 白→薄青
    outTopColor = {0.8f, 0.85f, 0.95f};
    outHorizonColor = {0.85f, 0.9f, 0.98f};
    outBottomColor = {0.75f, 0.8f, 0.9f};
    break;

  case SkyboxTheme::Volcano: // 赤黒・溶岩
    outTopColor = {0.3f, 0.15f, 0.1f};
    outHorizonColor = {0.5f, 0.2f, 0.1f};
    outBottomColor = {0.25f, 0.1f, 0.05f};
    break;

  case SkyboxTheme::Urban: // 都会の霞
    outTopColor = {0.55f, 0.55f, 0.6f};
    outHorizonColor = {0.65f, 0.65f, 0.7f};
    outBottomColor = {0.5f, 0.5f, 0.55f};
    break;

  case SkyboxTheme::Sunset: // 夕焼け
    outTopColor = {0.6f, 0.3f, 0.4f};
    outHorizonColor = {0.8f, 0.5f, 0.3f};
    outBottomColor = {0.5f, 0.25f, 0.35f};
    break;

  case SkyboxTheme::Sports: // 鮮やかな青空
    outTopColor = {0.3f, 0.5f, 0.9f};
    outHorizonColor = {0.6f, 0.75f, 0.95f};
    outBottomColor = {0.5f, 0.65f, 0.85f};
    break;

  case SkyboxTheme::Art: // パステル調
    outTopColor = {0.7f, 0.6f, 0.75f};
    outHorizonColor = {0.8f, 0.75f, 0.85f};
    outBottomColor = {0.65f, 0.55f, 0.7f};
    break;

  case SkyboxTheme::Music: // リズミカルな色
    outTopColor = {0.5f, 0.4f, 0.7f};
    outHorizonColor = {0.6f, 0.5f, 0.8f};
    outBottomColor = {0.45f, 0.35f, 0.65f};
    break;

  case SkyboxTheme::Literature: // クリーム色
    outTopColor = {0.75f, 0.7f, 0.6f};
    outHorizonColor = {0.85f, 0.8f, 0.7f};
    outBottomColor = {0.7f, 0.65f, 0.55f};
    break;

  case SkyboxTheme::Medical: // クリーンな白緑
    outTopColor = {0.75f, 0.8f, 0.75f};
    outHorizonColor = {0.85f, 0.9f, 0.85f};
    outBottomColor = {0.7f, 0.75f, 0.7f};
    break;

  case SkyboxTheme::Food: // 暖色系
    outTopColor = {0.8f, 0.6f, 0.4f};
    outHorizonColor = {0.9f, 0.75f, 0.5f};
    outBottomColor = {0.75f, 0.55f, 0.35f};
    break;

  case SkyboxTheme::Religion: // 神秘的な紫金
    outTopColor = {0.5f, 0.4f, 0.6f};
    outHorizonColor = {0.65f, 0.55f, 0.5f};
    outBottomColor = {0.45f, 0.35f, 0.55f};
    break;

  case SkyboxTheme::War: // 暗いグレー・煙
    outTopColor = {0.35f, 0.35f, 0.35f};
    outHorizonColor = {0.45f, 0.45f, 0.45f};
    outBottomColor = {0.3f, 0.3f, 0.3f};
    break;

  case SkyboxTheme::Fantasy: // 魔法的
    outTopColor = {0.6f, 0.4f, 0.75f};
    outHorizonColor = {0.7f, 0.6f, 0.85f};
    outBottomColor = {0.55f, 0.35f, 0.7f};
    break;

  case SkyboxTheme::Horror: // 不気味な暗紫
    outTopColor = {0.25f, 0.15f, 0.3f};
    outHorizonColor = {0.35f, 0.2f, 0.35f};
    outBottomColor = {0.2f, 0.1f, 0.25f};
    break;

  case SkyboxTheme::SciFi: // ネオンカラー
    outTopColor = {0.2f, 0.5f, 0.7f};
    outHorizonColor = {0.3f, 0.6f, 0.8f};
    outBottomColor = {0.15f, 0.45f, 0.65f};
    break;

  case SkyboxTheme::Retro: // ヴィンテージ
    outTopColor = {0.6f, 0.55f, 0.45f};
    outHorizonColor = {0.7f, 0.65f, 0.55f};
    outBottomColor = {0.55f, 0.5f, 0.4f};
    break;

  default:
    outTopColor = {0.4f, 0.6f, 0.9f};
    outHorizonColor = {0.7f, 0.8f, 0.95f};
    outBottomColor = {0.6f, 0.7f, 0.85f};
    break;
  }
}


void SkyboxTextureGenerator::GenerateFaceData(
    const XMFLOAT3 &topColor, const XMFLOAT3 &horizonColor,
    const XMFLOAT3 &bottomColor, int faceSize,
    std::vector<std::vector<uint8_t>> &outData, SkyboxTheme theme) {

  outData.resize(6); // 6面

  std::srand(12345);

  ThemeParams params = GetThemeParams(theme);

  for (int face = 0; face < 6; ++face) {
    outData[face].resize(faceSize * faceSize * 4); // RGBA

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

        XMFLOAT3 sunDir = {0.7f, 0.5f, 0.3f}; // デフォルト

        float noise =
            GenerateNoise(dir.x * 5.0f, dir.y * 5.0f, dir.z * 5.0f) *
            params.noiseMult;
        float starIntensity =
            GenerateStars(dir.x, dir.y, dir.z) * params.starMult;
        float galaxyIntensity =
            GenerateGalaxy(dir.x, dir.y, dir.z) * params.galaxyMult;
        float sunIntensity =
            GenerateSun(dir, sunDir) * (params.sunSize / 0.015f);

        float fbmCloud =
            GenerateFBM(dir.x * 2.0f, dir.y * 2.0f, dir.z * 2.0f, 5);
        float worleyCloud =
            GenerateWorleyNoise(dir.x * 3.0f, dir.y * 3.0f, dir.z * 3.0f);
        float cloudPattern =
            (fbmCloud * 0.6f + worleyCloud * 0.4f) * params.cloudMult;

        float detail =
            GenerateNoise(dir.x * 10.0f, dir.y * 10.0f, dir.z * 10.0f) * 0.1f;
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

        if (yFactor > -0.2f && yFactor < 0.8f && params.cloudMult > 0.0f) {
          float cloudBase = cloudPattern * 0.2f * params.cloudMult;
          float lightingFactor = std::max(0.0f, -dir.x * 0.5f + 0.5f);
          float cloudHighlight = cloudPattern * cloudPattern * 0.3f *
                                 lightingFactor * params.cloudMult;

          color.x += cloudBase + cloudHighlight;
          color.y += cloudBase + cloudHighlight;
          color.z += cloudBase + cloudHighlight;
        }

        float brightness = (color.x + color.y + color.z) / 3.0f;
        if (brightness < 0.3f && starIntensity > 0.9f &&
            params.starMult > 0.0f) {
          float starBoost =
              (starIntensity - 0.9f) * 10.0f * params.starMult;
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
          float nebula = GenerateFBM(dir.x * 4.0f, dir.y * 2.0f,
                                     dir.z * 4.0f, 6);
          nebula = std::pow(nebula, 3.0f) * params.nebulaStrength;
          color.x += nebula * params.nebulaColor.x;
          color.y += nebula * params.nebulaColor.y;
          color.z += nebula * params.nebulaColor.z;
        }

        // リボン状の光（オーロラ/ネオン帯）
        if (params.ribbonStrength > 0.0f) {
          float ribbonWave = std::sin(dir.x * params.ribbonFrequency) *
                             std::cos(dir.z * params.ribbonFrequency * 0.7f);
          float ribbon = std::pow(std::abs(ribbonWave), params.ribbonSharpness) *
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
        float vignette = std::pow(std::min(1.0f, radius), 2.2f) * params.vignette;
        color.x *= (1.0f - vignette);
        color.y *= (1.0f - vignette);
        color.z *= (1.0f - vignette);

        float dither = HashNoise(x, y, face) - 0.5f;
        color.x = std::max(0.0f, std::min(1.0f, color.x + dither * 0.003f));
        color.y = std::max(0.0f, std::min(1.0f, color.y + dither * 0.003f));
        color.z = std::max(0.0f, std::min(1.0f, color.z + dither * 0.003f));

        // RGB値を0-255に変換
        int idx = (y * faceSize + x) * 4;
        outData[face][idx + 0] = static_cast<uint8_t>(color.x * 255.0f);
        outData[face][idx + 1] = static_cast<uint8_t>(color.y * 255.0f);
        outData[face][idx + 2] = static_cast<uint8_t>(color.z * 255.0f);
        outData[face][idx + 3] = 255; // Alpha
      }
    }
  }
}
bool SkyboxTextureGenerator::CreateCubemapTexture(
    ID3D11Device *device, const std::vector<std::vector<uint8_t>> &faceData,
    int faceSize, ComPtr<ID3D11ShaderResourceView> &outSRV) {

  if (!device || faceData.size() != 6) {
    return false;
  }

  // テクスチャ記述
  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = faceSize;
  texDesc.Height = faceSize;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 6; // キューブマップは6面
  texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

  // 初期データ
  D3D11_SUBRESOURCE_DATA initData[6];
  for (int i = 0; i < 6; ++i) {
    initData[i].pSysMem = faceData[i].data();
    initData[i].SysMemPitch = faceSize * 4; // RGBA
    initData[i].SysMemSlicePitch = 0;
  }

  // テクスチャ作成
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&texDesc, initData, &texture);
  if (FAILED(hr)) {
    return false;
  }

  // ShaderResourceView作成
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = texDesc.Format;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
  srvDesc.TextureCube.MipLevels = 1;
  srvDesc.TextureCube.MostDetailedMip = 0;

  hr = device->CreateShaderResourceView(texture.Get(), &srvDesc, &outSRV);
  if (FAILED(hr)) {
    return false;
  }

  return true;
}

} // namespace graphics
