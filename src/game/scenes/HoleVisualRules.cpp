/**
 * @file HoleVisualRules.cpp
 * @brief ホール表示色の規則を実装します。
*/

#include "HoleVisualRules.h"

namespace game::scenes {

DirectX::XMFLOAT4 HoleVisualRules::GetColor(bool isTargetHole,
                                            int hopsToTarget) {
    if (isTargetHole) {
        return {1.0f, 0.2f, 0.2f, 1.0f};
    }
    if (hopsToTarget == 1) {
        return {1.0f, 0.85f, 0.0f, 1.0f};
    }
    if (hopsToTarget == 2) {
        return {1.0f, 0.6f, 0.2f, 1.0f};
    }
    if (hopsToTarget >= 3 && hopsToTarget <= 5) {
        return {0.95f, 0.95f, 0.95f, 1.0f};
    }
    if (hopsToTarget > 5) {
        return {0.6f, 0.6f, 0.6f, 1.0f};
    }
    return {0.25f, 0.65f, 1.0f, 1.0f};
}

DirectX::XMFLOAT4 HoleVisualRules::GetBodyColor(bool isTargetHole,
                                                int hopsToTarget) {
    DirectX::XMFLOAT4 color = GetColor(isTargetHole, hopsToTarget);
    color.x *= 0.45f;
    color.y *= 0.45f;
    color.z *= 0.45f;
    color.w = 1.0f;
    return color;
}

} // namespace game::scenes
