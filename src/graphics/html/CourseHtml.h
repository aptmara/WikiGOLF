#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace graphics::html {
inline constexpr std::size_t kMaxHtmlBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxImages = 12;
inline constexpr std::uint64_t kMaxTexturePixels = 32ull * 1024 * 1024;
// 長大な記事でレイアウト高さ（＝コース奥行き）が肥大化しすぎないよう、
// 横幅を広げて1行あたりの文字数を増やし、縦方向の伸びを抑える。
inline constexpr int kLayoutWidth = 2048 * 3;

struct PreparedArticle {
    std::string html;
    std::vector<std::string> imageUrls;
    std::string error;
};
struct Rect { float x = 0, y = 0, width = 0, height = 0; };
struct Link {
    std::uint32_t elementId = 0;
    std::string target;
    std::vector<Rect> fragments;
    Rect representative;
};
enum class RegionKind { Heading, Image, Table };
struct Region { Rect rect; RegionKind kind; int level = 0; };
struct RasterSize {
    std::uint32_t width = 0, height = 0;
    float scaleX = 0, scaleY = 0;
};
/** @brief 記事名前空間の内部リンクを復号・正規化します。対象外は空文字を返します。 */
std::string ArticleTarget(const std::string& href);
/** @brief 許可した記事構造のみ保持し、外部CSS・スクリプトを除外します。 */
PreparedArticle PrepareArticle(const std::string& title, const std::string& html);
/** @brief 固定レイアウトに対する画素上限付き出力寸法を返します。 */
RasterSize FitRaster(float width, float height,
                     std::uint64_t maxPixels = kMaxTexturePixels);
const std::string& CourseCss();
}
