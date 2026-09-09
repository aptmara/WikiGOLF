#pragma once
#include <algorithm>

namespace game::scenes {
/** @brief HTMLレイアウトの縦横比を保ったまま、記事文字数由来の希望幅を
 *         最小/最大サイズへ収める。縦だけ・横だけを個別にクランプすると
 *         テクスチャ（記事本文）が縦横に引き伸ばされて見えるため、
 *         範囲外になった場合は幅と奥行きを同じ係数でまとめてスケールする。 */
inline void ResolveHtmlFieldSize(float desiredWidth, float layoutWidth, float layoutHeight,
    float minWidth, float minDepth, float maxWidth, float maxDepth,
    float& outWidth, float& outDepth) {
    if (layoutWidth <= 0.0f) {
        outWidth = desiredWidth;
        outDepth = minDepth;
        return;
    }
    const float aspect = layoutHeight / layoutWidth;
    float width = std::max(desiredWidth, 1e-3f);
    float depth = width * aspect;

    float growFix = 1.0f;
    if (width < minWidth) growFix = std::max(growFix, minWidth / width);
    if (depth < minDepth) growFix = std::max(growFix, minDepth / depth);
    width *= growFix;
    depth *= growFix;

    float shrinkFix = 1.0f;
    if (width > maxWidth) shrinkFix = std::min(shrinkFix, maxWidth / width);
    if (depth > maxDepth) shrinkFix = std::min(shrinkFix, maxDepth / depth);
    outWidth = width * shrinkFix;
    outDepth = depth * shrinkFix;
}
}
