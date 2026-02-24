#include "src/graphics/WikiTextureGenerator.h"
#include <d3d11.h>
#include <iostream>
#include <string>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      return 1;                                                                \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using Microsoft::WRL::ComPtr;

  // DirectXデバイスをWARPで生成（GPU依存を避ける）
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL featureLevel;
  UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
  createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags, nullptr, 0,
      D3D11_SDK_VERSION, &device, &featureLevel, &context);
  if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG)) {
    // デバッグレイヤー未インストール環境向けフォールバック
    createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags,
                           nullptr, 0, D3D11_SDK_VERSION, &device,
                           &featureLevel, &context);
  }
  CHECK(SUCCEEDED(hr), "Create D3D11 device (WARP)");

  graphics::WikiTextureGenerator generator;
  CHECK(generator.Initialize(device.Get()), "Initialize WikiTextureGenerator");

  // Alpha は本文に存在するが、Gamma は本文に存在しない
  std::wstring articleText = L"Alpha link is here.";
  std::vector<std::pair<std::wstring, std::string>> links = {
      {L"Alpha", "Alpha"}, {L"Gamma", "Gamma"}};

  auto result =
      generator.GenerateTexture(L"Sample", articleText, links, "Gamma", 512,
                                512);

  bool hasAlpha = false;
  bool hasGamma = false;
  for (const auto &link : result.links) {
    if (link.targetPage == "Alpha")
      hasAlpha = true;
    if (link.targetPage == "Gamma")
      hasGamma = true;
  }

  CHECK(hasAlpha, "Matched link should create a region");
  CHECK(hasGamma, "Unmatched link should fall back to see-also region");

  generator.Shutdown();
  std::cout << "All WikiTexture link tests passed.\n";
  return 0;
}
