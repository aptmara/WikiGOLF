#include "src/game/scenes/CupInUtils.h"
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      std::exit(1);                                                            \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using game::scenes::cupin::IsBallReadyForCupIn;

  DirectX::XMFLOAT3 hole{0.0f, 0.0f, 0.0f};

  CHECK(!IsBallReadyForCupIn({0.0f, 0.25f, 0.0f}, hole, 0.8f, 0.0001f),
        "High-above ball should not trigger cup-in");

  CHECK(IsBallReadyForCupIn({0.1f, -0.05f, 0.1f}, hole, 0.8f, 0.0005f),
        "Settled ball inside capture radius should cup-in");

  CHECK(!IsBallReadyForCupIn({0.1f, -0.05f, 0.1f}, hole, 0.8f, 0.2f),
        "Fast-moving ball should wait until it slows down");

  std::cout << "All cup-in utility tests passed!\n";
  return 0;
}
