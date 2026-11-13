#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace graphics::html {
inline constexpr std::size_t kMaxHtmlBytes = 6 * 1024 * 1024;
inline constexpr std::size_t kMaxImages = 12;
/** ライセンス確認の対象にする画像数の上限（確認のリクエストを増やしすぎないため） */
inline constexpr std::size_t kMaxImageCandidates = 100;
inline constexpr std::uint64_t kMaxTexturePixels = 64ull * 1024 * 1024;
inline constexpr int kLayoutWidth = 2048;

struct PreparedArticle {
    std::string html;
    std::vector<std::string> imageUrls;
    std::string error;
    /** 表示する本文のプレーンテキスト（ブロック要素の区切りは改行） */
    std::string text;
    /** 表示する本文に現れる記事リンクの宛先（出現順・重複なし） */
    std::vector<std::string> linkTargets;
    /** 記事のカテゴリ（"Category:名前" 形式・出現順） */
    std::vector<std::string> categories;
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
/**
 * @brief カテゴリを表す link 要素の href から "Category:名前" を取り出します。
 * @details 名前空間の表記（Category / カテゴリ）は "Category" にそろえ、ソートキーは捨てます。
 * @return カテゴリと判定できなければ空文字列
 */
std::string CategoryTitle(const std::string& href);
/**
 * @brief 許可した記事構造のみ保持し、外部CSS・スクリプトを除外します。
 * @param allowedImageUrls 指定すると、ここに含まれる画像だけを残します。
 */
PreparedArticle PrepareArticle(const std::string& title, const std::string& html,
                               const std::unordered_set<std::string>* allowedImageUrls = nullptr);
/** @brief ライセンス確認の対象となる画像URLを、記事内の出現順に返します。 */
std::vector<std::string> ExtractImageUrls(const std::string& html);
/**
 * @brief 画像URLからファイル名を取り出します（例: "Golfer_swing.jpg"）。
 * @return WikipediaまたはCommonsのファイルと判定できなければ空文字列
 */
std::string ImageFileName(const std::string& url);
/** @brief 固定レイアウトに対する画素上限付き出力寸法を返します。 */
RasterSize FitRaster(float width, float height,
                     std::uint64_t maxPixels = kMaxTexturePixels);
const std::string& CourseCss();
}
