#include "src/graphics/WikiTextureGenerator.h"
#include <d3d11.h>
#include <iostream>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                              \
      return 1;                                                               \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (false)

namespace {

bool CreateWarpDevice(Microsoft::WRL::ComPtr<ID3D11Device> &device,
                      Microsoft::WRL::ComPtr<ID3D11DeviceContext> &context) {
  D3D_FEATURE_LEVEL featureLevel;
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
      D3D11_SDK_VERSION, &device, &featureLevel, &context);
  return SUCCEEDED(hr);
}

} // namespace

int main() {
  using Microsoft::WRL::ComPtr;

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  CHECK(CreateWarpDevice(device, context), "Create D3D11 device (WARP)");

  graphics::WikiTextureGenerator generator;
  CHECK(generator.Initialize(device.Get()), "Initialize WikiTextureGenerator");

  graphics::PendingWikiImage leadImage;
  leadImage.pixelWidth = 2;
  leadImage.pixelHeight = 2;
  leadImage.pixelsBGRA.assign(16, 255);
  leadImage.caption = L"Lead image";
  leadImage.isLead = true;

  const std::wstring article = L"== Heading ==\nAlpha link is here.";
  const std::vector<std::pair<std::wstring, std::string>> links = {
      {L"Alpha", "Alpha"}};

  const graphics::WikiTextureResult result = generator.GenerateTexture(
      L"Sample", article, links, "Alpha", 512, 256, {leadImage});
  CHECK(result.width == 512, "Generated texture width is preserved");
  CHECK(result.height >= 256, "Generated texture height respects request");
  CHECK(!result.tiles.empty(), "Generated texture contains tiles");
  CHECK(result.images.size() == 1, "Lead image creates one image region");
  CHECK(!result.headings.empty(), "Heading creates one or more heading regions");
  CHECK(!result.links.empty(), "Article link creates a link region");
  CHECK(result.links.front().isTarget, "Target link is marked as target");

  graphics::WikiTextureGenerationState state;
  CHECK(generator.BeginGenerateTexture(state, L"Sample", article, links,
                                       "Alpha", 512, 256, {leadImage}),
        "Begin incremental texture generation");

  int tileCalls = 0;
  bool completed = false;
  while (!completed && tileCalls < 100) {
    completed = generator.GenerateNextTile(state);
    ++tileCalls;
  }
  CHECK(completed, "Incremental generation completes");
  CHECK(tileCalls == static_cast<int>(state.result.tiles.size()),
        "Each incremental call creates one tile");

  const size_t completedTileCount = state.result.tiles.size();
  CHECK(generator.GenerateNextTile(state),
        "Completed generation remains completed");
  CHECK(state.result.tiles.size() == completedTileCount,
        "Completed generation does not create duplicate tiles");

  generator.Shutdown();
  std::cout << "All WikiTexture generation tests passed.\n";
  return 0;
}
