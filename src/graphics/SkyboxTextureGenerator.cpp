/**
 * @file SkyboxTextureGenerator.cpp
 * @brief SkyboxTextureGenerator の実装
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

const wchar_t *kSkyboxFaceSuffixes[6] = {L"_px.png", L"_nx.png", L"_py.png",
                                         L"_ny.png", L"_pz.png", L"_nz.png"};

std::wstring SkyboxTextureGenerator::GetThemeFileName(SkyboxTheme theme) {
  switch (theme) {
  case SkyboxTheme::Default:
    return L"Default";
  case SkyboxTheme::HistoryAncient:
    return L"history";
  case SkyboxTheme::Medieval:
    return L"medieval";
  case SkyboxTheme::ScienceTech:
    return L"science";
  case SkyboxTheme::SpaceAstronomy:
    return L"space";
  case SkyboxTheme::Ocean:
    return L"ocean";
  case SkyboxTheme::Mountain:
    return L"mountain";
  case SkyboxTheme::Forest:
    return L"forest";
  case SkyboxTheme::Desert:
    return L"desert";
  case SkyboxTheme::Polar:
    return L"polar";
  case SkyboxTheme::Volcano:
    return L"volcano";
  case SkyboxTheme::Urban:
    return L"urban";
  case SkyboxTheme::Sunset:
    return L"sunset";
  case SkyboxTheme::Sports:
    return L"sports";
  case SkyboxTheme::Art:
    return L"art";
  case SkyboxTheme::Music:
    return L"music";
  case SkyboxTheme::Literature:
    return L"literature";
  case SkyboxTheme::Medical:
    return L"medical";
  case SkyboxTheme::Food:
    return L"food";
  case SkyboxTheme::Religion:
    return L"religion";
  case SkyboxTheme::War:
    return L"war";
  case SkyboxTheme::Fantasy:
    return L"fantasy";
  case SkyboxTheme::Horror:
    return L"horror";
  case SkyboxTheme::SciFi:
    return L"scifi";
  case SkyboxTheme::Retro:
    return L"retro";
  case SkyboxTheme::GolfCourseClear:
    return L"golf_course_clear";
  default:
    return L"Default";
  }
}

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

  const int faceSize = skybox_detail::kDefaultFaceSize; // 各面512x512
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
  GenerateFaceData(topColor, horizonColor, bottomColor, skybox_detail::kDefaultFaceSize,
                   faceData, theme);

  std::filesystem::path basePath(baseFilePath);
  if (!basePath.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(basePath.parent_path(), ec);
  }

  bool savedAll = true;
  for (int i = 0; i < 6; ++i) {
    std::filesystem::path facePath = basePath;
    facePath += kSkyboxFaceSuffixes[i];
    if (!skybox_detail::SaveFaceToFile(faceData[i], skybox_detail::kDefaultFaceSize, facePath.wstring())) {
      savedAll = false;
    }
  }

  if (savedAll) {
    return LoadCubemapFromFiles(device, baseFilePath, outSRV);
  }

  // 保存に失敗した場合は生成データから直接作成する
  return CreateCubemapTexture(device, faceData, skybox_detail::kDefaultFaceSize, outSRV);
}

bool SkyboxTextureGenerator::GenerateCubemapFromThemeToFiles(
    ID3D11Device *device, SkyboxTheme theme, const std::wstring &baseFilePath) {

  std::filesystem::path basePath(baseFilePath);

  // どの面が欠けているかチェック
  uint8_t faceMask = 0;
  for (int i = 0; i < 6; ++i) {
    std::filesystem::path facePath = basePath;
    facePath += kSkyboxFaceSuffixes[i];
    if (!std::filesystem::exists(facePath)) {
      faceMask |= (1 << i);
    }
  }

  // すべて存在すればスキップ
  if (faceMask == 0) {
    return true;
  }

  XMFLOAT3 topColor, horizonColor, bottomColor;
  GetThemeColors(theme, topColor, horizonColor, bottomColor);

  std::vector<std::vector<uint8_t>> faceData;
  GenerateFaceData(topColor, horizonColor, bottomColor, skybox_detail::kDefaultFaceSize,
                   faceData, theme, faceMask);

  if (!basePath.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(basePath.parent_path(), ec);
  }

  bool savedAll = true;
  for (int i = 0; i < 6; ++i) {
    if (!(faceMask & (1 << i))) {
      continue;
    }

    std::filesystem::path facePath = basePath;
    facePath += kSkyboxFaceSuffixes[i];
    if (!skybox_detail::SaveFaceToFile(faceData[i], skybox_detail::kDefaultFaceSize, facePath.wstring())) {
      savedAll = false;
    }
  }

  return savedAll;
}

bool SkyboxTextureGenerator::LoadCubemapFromFiles(
    ID3D11Device *device, const std::wstring &baseFilePath,
    ComPtr<ID3D11ShaderResourceView> &outSRV) {

  std::vector<std::vector<uint8_t>> faceData(6);
  for (int i = 0; i < 6; ++i) {
    std::filesystem::path facePath(baseFilePath);
    facePath += kSkyboxFaceSuffixes[i];
    if (!std::filesystem::exists(facePath)) {
      return false;
    }

    if (!skybox_detail::LoadFaceFromFile(facePath.wstring(), skybox_detail::kDefaultFaceSize, faceData[i])) {
      return false;
    }
  }

  return CreateCubemapTexture(device, faceData, skybox_detail::kDefaultFaceSize, outSRV);
}

bool SkyboxTextureGenerator::LoadCubemapFromSingleFile(
    ID3D11Device *device, const std::wstring &filePath,
    ComPtr<ID3D11ShaderResourceView> &outSRV) {

  std::vector<uint8_t> data;
  if (!skybox_detail::LoadFaceFromFile(filePath, skybox_detail::kDefaultFaceSize, data)) {
    return false;
  }

  // すべての面に同じデータを設定
  std::vector<std::vector<uint8_t>> faceData(6, data);
  return CreateCubemapTexture(device, faceData, skybox_detail::kDefaultFaceSize, outSRV);
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
    initData[i].SysMemPitch = faceSize * 4; // RGBA形式
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
