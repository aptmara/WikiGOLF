#pragma once

/**
 * @file HoleVisualRules.h
 * @brief ホールの距離・目的地に応じた表示色の規則を定義します。
 */

#include <DirectXMath.h>

namespace game::scenes {

/**
 * @brief ホール表示で共有する色計算を提供します。
 */
class HoleVisualRules {
public:
    /**
     * @brief 目的地からの距離に応じた表示色を返します。
     * @param isTargetHole 目的地ホールかどうかです。
     * @param hopsToTarget 目的地までのホップ数です。
     */
    static DirectX::XMFLOAT4 GetColor(bool isTargetHole, int hopsToTarget);

    /**
     * @brief ホール本体用に暗くした表示色を返します。
     * @param isTargetHole 目的地ホールかどうかです。
     * @param hopsToTarget 目的地までのホップ数です。
     */
    static DirectX::XMFLOAT4 GetBodyColor(bool isTargetHole,
                                          int hopsToTarget);
};

} // namespace game::scenes
