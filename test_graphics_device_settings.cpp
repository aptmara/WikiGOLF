#include "src/graphics/GraphicsDevice.h"
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
    std::cout << "[PASS] " << message << "\n";                              \
  } while (false)

int main() {
  graphics::GraphicsDevice device;
  const graphics::QualitySettings &quality = device.GetQualitySettings();

  CHECK(device.GetWidth() == 0, "Uninitialized device width remains zero");
  CHECK(device.GetHeight() == 0, "Uninitialized device height remains zero");
  CHECK(quality.renderScale == 1.0f, "Default render scale is one");
  CHECK(quality.msaaSamples == 1, "Default MSAA is disabled");
  CHECK(!quality.fxaaEnabled, "Default FXAA is disabled");
  CHECK(device.GetVSync(), "VSync is enabled by default");
  CHECK(!device.IsFullscreenExclusive(),
        "Exclusive fullscreen is disabled by default");
  CHECK(!device.IsDepthReadable(),
        "Depth SRV is unavailable before initialization");

  std::cout << "All graphics device setting tests passed!\n";
  return 0;
}
