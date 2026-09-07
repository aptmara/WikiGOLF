#include "src/graphics/TextRenderer.h"
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                              \
      return 1;                                                               \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (false)

int main() {
  graphics::TextRenderer renderer;
  CHECK(!renderer.IsValid(), "Renderer starts invalid");
  CHECK(renderer.GetWidth() == 1280.0f, "Virtual width is preserved");
  CHECK(renderer.GetHeight() == 720.0f, "Virtual height is preserved");
  CHECK(!renderer.Initialize(nullptr), "Null swap chain is rejected");

  renderer.BeginDraw();
  renderer.EndDraw();
  renderer.FillRect(D2D1::RectF(0.0f, 0.0f, 10.0f, 10.0f),
                    DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
  renderer.FillFullScreenRect(DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
  renderer.RenderText(L"", 0.0f, 0.0f, graphics::TextStyle{});
  renderer.RenderTextCached(L"", D2D1::RectF(0.0f, 0.0f, 10.0f, 10.0f),
                            graphics::TextStyle{});
  CHECK(!renderer.LoadBitmapFromFile(""), "Empty bitmap path is rejected");
  CHECK(!renderer.RecreateTargetAfterResize(),
        "Resize recreation requires a swap chain");

  renderer.ReleaseTargetForResize();
  renderer.Shutdown();
  renderer.Shutdown();
  std::cout << "All TextRenderer lifecycle tests passed.\n";
  return 0;
}
