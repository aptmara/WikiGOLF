#include "src/game/components/UIImage.h"
#include <iostream>

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
  using game::components::UIImage;

  UIImage ui{};
  CHECK(!ui.HasTexture(),
        "HasTexture is false when both texturePath and textureSRV are empty");
  CHECK(!ui.grayscaleTint && ui.tintColor.x == 1.0f &&
            ui.tintColor.y == 1.0f && ui.tintColor.z == 1.0f,
        "UIImage color processing is disabled by default");

  ui.grayscaleTint = true;
  ui.tintColor = {1.0f, 0.2f, 0.2f, 1.0f};
  CHECK(ui.grayscaleTint && ui.tintColor.y == 0.2f,
        "UIImage stores grayscale tint settings");

  ui.texturePath = "icon.png";
  CHECK(ui.HasTexture(), "HasTexture is true when texturePath is set");

  ui.texturePath.clear();
  ui.textureSRV = reinterpret_cast<ID3D11ShaderResourceView *>(0x1);
  CHECK(ui.HasTexture(), "HasTexture is true when textureSRV is set");

  ui.textureSRV = nullptr;
  CHECK(!ui.HasTexture(),
        "HasTexture returns false again after clearing both sources");

  std::cout << "All UIImage texture availability tests passed.\n";
  return 0;
}
