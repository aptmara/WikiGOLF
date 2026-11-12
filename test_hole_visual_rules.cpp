#include "src/game/scenes/HoleVisualRules.h"

#include <cassert>

namespace {

bool SameColor(const DirectX::XMFLOAT4& left,
               const DirectX::XMFLOAT4& right) {
  return left.x == right.x && left.y == right.y && left.z == right.z &&
         left.w == right.w;
}

} // namespace

int main() {
  using game::scenes::HoleVisualRules;

  assert(!SameColor(HoleVisualRules::GetColor(true, 0),
                    HoleVisualRules::GetColor(false, 1)));

  const DirectX::XMFLOAT4 oniColor =
      HoleVisualRules::GetColor(false, -1, true);
  assert(SameColor(HoleVisualRules::GetColor(true, 0),
                   HoleVisualRules::GetColor(true, 0, true)));
  assert(!SameColor(oniColor, HoleVisualRules::GetColor(true, 0, true)));
  assert(SameColor(oniColor, HoleVisualRules::GetColor(false, 1, true)));
  assert(SameColor(oniColor, HoleVisualRules::GetColor(false, 8, true)));

  return 0;
}
