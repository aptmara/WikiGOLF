#include "SkyboxTextureGenerator.h"
#include "SkyboxTextureGeneratorInternals.h"
#include <algorithm>
#include <cctype>
#include <combaseapi.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include <wincodec.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;


namespace graphics {

SkyboxTheme
SkyboxTextureGenerator::DetermineTheme(const std::string &pageTitle,
                                       const std::string &pageExtract) {

  std::string combined = pageTitle + " " + pageExtract;

  // 優先度順にテーマ判定（より具体的なものから）

  // 宇宙・天文
  if (skybox_detail::ContainsAnyKeyword(combined, {"宇宙", "天文", "惑星", "星", "銀河", "月",
                                    "太陽系", "ブラックホール", "彗星", "space",
                                    "astronomy", "planet", "galaxy"})) {
    return SkyboxTheme::SpaceAstronomy;
  }

  // 海洋・水中
  if (skybox_detail::ContainsAnyKeyword(combined, {"海", "海洋", "水中", "深海", "サンゴ礁",
                                    "イルカ", "クジラ", "魚", "ocean", "sea",
                                    "underwater", "marine"})) {
    return SkyboxTheme::Ocean;
  }

  // 火山
  if (skybox_detail::ContainsAnyKeyword(combined, {"火山", "噴火", "溶岩", "マグマ", "volcano",
                                    "lava", "eruption"})) {
    return SkyboxTheme::Volcano;
  }

  // 極地・雪
  if (skybox_detail::ContainsAnyKeyword(combined, {"極地", "南極", "北極", "氷河", "雪",
                                    "オーロラ", "polar", "arctic", "antarctica",
                                    "glacier", "snow"})) {
    return SkyboxTheme::Polar;
  }

  // 砂漠
  if (skybox_detail::ContainsAnyKeyword(combined, {"砂漠", "サハラ", "乾燥", "オアシス",
                                    "desert", "sahara", "dune"})) {
    return SkyboxTheme::Desert;
  }

  // 森林・ジャングル
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"森", "森林", "ジャングル", "熱帯雨林", "木",
                          "forest", "jungle", "rainforest", "woods"})) {
    return SkyboxTheme::Forest;
  }

  // 山岳・登山
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"山", "登山", "高山", "ヒマラヤ", "アルプス",
                          "mountain", "climbing", "peak", "summit"})) {
    return SkyboxTheme::Mountain;
  }

  // ホラー・オカルト
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"ホラー", "幽霊", "お化け", "オカルト", "怪談",
                          "呪い", "horror", "ghost", "haunted", "occult"})) {
    return SkyboxTheme::Horror;
  }

  // ファンタジー
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"魔法", "魔術", "ドラゴン", "エルフ", "ファンタジー",
                          "fantasy", "magic", "wizard", "dragon"})) {
    return SkyboxTheme::Fantasy;
  }

  // SF・未来
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"SF", "サイエンスフィクション", "未来", "ロボット",
                          "AI", "サイバー", "sci-fi", "science fiction",
                          "cyberpunk", "futuristic"})) {
    return SkyboxTheme::SciFi;
  }

  // 戦争・軍事
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"戦争", "軍事", "兵器", "戦闘", "軍隊", "war",
                          "military", "battle", "weapon", "soldier"})) {
    return SkyboxTheme::War;
  }

  // 中世・城
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"中世", "城", "騎士", "王国", "貴族", "medieval",
                          "castle", "knight", "kingdom"})) {
    return SkyboxTheme::Medieval;
  }

  // 歴史・古代
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"歴史", "古代", "遺跡", "文明", "考古学", "history",
                          "ancient", "civilization", "archaeology", "ruins"})) {
    return SkyboxTheme::HistoryAncient;
  }

  // 宗教・神話
  if (skybox_detail::ContainsAnyKeyword(combined, {"宗教", "神", "仏教", "キリスト教", "神話",
                                    "寺", "教会", "religion", "god",
                                    "mythology", "temple", "shrine"})) {
    return SkyboxTheme::Religion;
  }

  // 都市・建築
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"都市", "建築", "ビル", "摩天楼", "都会", "city",
                          "urban", "building", "architecture", "skyscraper"})) {
    return SkyboxTheme::Urban;
  }

  // 医療・生物
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"医療", "医学", "病院", "生物", "細胞", "DNA",
                          "medical", "medicine", "biology", "hospital"})) {
    return SkyboxTheme::Medical;
  }

  // 科学・技術
  if (skybox_detail::ContainsAnyKeyword(combined, {"科学", "技術", "物理", "化学", "工学",
                                    "science", "technology", "physics",
                                    "chemistry", "engineering"})) {
    return SkyboxTheme::ScienceTech;
  }

  // 音楽
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"音楽", "楽器", "演奏", "コンサート", "オーケストラ",
                          "music", "instrument", "concert", "orchestra"})) {
    return SkyboxTheme::Music;
  }

  // 芸術・美術
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"芸術", "美術", "絵画", "彫刻", "芸術家", "art",
                          "painting", "sculpture", "artist", "gallery"})) {
    return SkyboxTheme::Art;
  }

  // 文学
  if (skybox_detail::ContainsAnyKeyword(combined,
                         {"文学", "小説", "詩", "作家", "文芸", "literature",
                          "novel", "poetry", "writer", "author"})) {
    return SkyboxTheme::Literature;
  }

  // 食品・料理
  if (skybox_detail::ContainsAnyKeyword(combined, {"料理", "食品", "レシピ", "グルメ",
                                    "レストラン", "food", "cooking", "cuisine",
                                    "recipe", "restaurant"})) {
    return SkyboxTheme::Food;
  }

  // スポーツ
  if (skybox_detail::ContainsAnyKeyword(combined, {"スポーツ", "競技", "選手", "オリンピック",
                                    "野球", "サッカー", "sports", "athlete",
                                    "olympics", "game"})) {
    return SkyboxTheme::Sports;
  }

  // 夕暮れ・夜
  if (skybox_detail::ContainsAnyKeyword(combined, {"夜", "夕暮れ", "夕焼け", "黄昏", "night",
                                    "sunset", "dusk", "twilight"})) {
    return SkyboxTheme::Sunset;
  }

  // レトロ
  if (skybox_detail::ContainsAnyKeyword(combined, {"レトロ", "昭和", "ヴィンテージ", "古い",
                                    "retro", "vintage", "classic", "old"})) {
    return SkyboxTheme::Retro;
  }

  // デフォルト（青空）
  return SkyboxTheme::Default;
}

void SkyboxTextureGenerator::GetThemeColors(SkyboxTheme theme,
                                            XMFLOAT3 &outTopColor,
                                            XMFLOAT3 &outHorizonColor,
                                            XMFLOAT3 &outBottomColor) {
  // 床の文字を見やすくするため、全体的に彩度と明度を抑えめに設定
  // RGB値を0-1の範囲で指定

  switch (theme) {
  case SkyboxTheme::Default: // 青空
    outTopColor = {0.4f, 0.6f, 0.9f};
    outHorizonColor = {0.7f, 0.8f, 0.95f};
    outBottomColor = {0.6f, 0.7f, 0.85f};
    break;

  case SkyboxTheme::HistoryAncient: // セピア調
    outTopColor = {0.55f, 0.45f, 0.35f};
    outHorizonColor = {0.65f, 0.55f, 0.45f};
    outBottomColor = {0.5f, 0.4f, 0.3f};
    break;

  case SkyboxTheme::Medieval: // 灰色・重厚
    outTopColor = {0.5f, 0.5f, 0.55f};
    outHorizonColor = {0.6f, 0.6f, 0.65f};
    outBottomColor = {0.45f, 0.45f, 0.5f};
    break;

  case SkyboxTheme::ScienceTech: // サイバーブルー
    outTopColor = {0.2f, 0.4f, 0.7f};
    outHorizonColor = {0.3f, 0.5f, 0.8f};
    outBottomColor = {0.15f, 0.35f, 0.65f};
    break;

  case SkyboxTheme::SpaceAstronomy: // 深い紫→黒
    outTopColor = {0.1f, 0.05f, 0.2f};
    outHorizonColor = {0.2f, 0.1f, 0.3f};
    outBottomColor = {0.05f, 0.03f, 0.15f};
    break;

  case SkyboxTheme::Ocean: // 深海ブルー
    outTopColor = {0.1f, 0.3f, 0.5f};
    outHorizonColor = {0.2f, 0.5f, 0.7f};
    outBottomColor = {0.05f, 0.2f, 0.4f};
    break;

  case SkyboxTheme::Mountain: // 高山の澄んだ空
    outTopColor = {0.5f, 0.7f, 0.95f};
    outHorizonColor = {0.75f, 0.85f, 0.98f};
    outBottomColor = {0.65f, 0.75f, 0.9f};
    break;

  case SkyboxTheme::Forest: // 緑がかった霧
    outTopColor = {0.4f, 0.5f, 0.45f};
    outHorizonColor = {0.5f, 0.6f, 0.55f};
    outBottomColor = {0.35f, 0.45f, 0.4f};
    break;

  case SkyboxTheme::Desert: // 砂色→オレンジ
    outTopColor = {0.7f, 0.55f, 0.3f};
    outHorizonColor = {0.8f, 0.65f, 0.4f};
    outBottomColor = {0.65f, 0.5f, 0.25f};
    break;

  case SkyboxTheme::Polar: // 白→薄青
    outTopColor = {0.8f, 0.85f, 0.95f};
    outHorizonColor = {0.85f, 0.9f, 0.98f};
    outBottomColor = {0.75f, 0.8f, 0.9f};
    break;

  case SkyboxTheme::Volcano: // 赤黒・溶岩
    outTopColor = {0.3f, 0.15f, 0.1f};
    outHorizonColor = {0.5f, 0.2f, 0.1f};
    outBottomColor = {0.25f, 0.1f, 0.05f};
    break;

  case SkyboxTheme::Urban: // 都会の霞
    outTopColor = {0.55f, 0.55f, 0.6f};
    outHorizonColor = {0.65f, 0.65f, 0.7f};
    outBottomColor = {0.5f, 0.5f, 0.55f};
    break;

  case SkyboxTheme::Sunset: // 夕焼け
    outTopColor = {0.6f, 0.3f, 0.4f};
    outHorizonColor = {0.8f, 0.5f, 0.3f};
    outBottomColor = {0.5f, 0.25f, 0.35f};
    break;

  case SkyboxTheme::Sports: // 鮮やかな青空
    outTopColor = {0.3f, 0.5f, 0.9f};
    outHorizonColor = {0.6f, 0.75f, 0.95f};
    outBottomColor = {0.5f, 0.65f, 0.85f};
    break;

  case SkyboxTheme::Art: // パステル調
    outTopColor = {0.7f, 0.6f, 0.75f};
    outHorizonColor = {0.8f, 0.75f, 0.85f};
    outBottomColor = {0.65f, 0.55f, 0.7f};
    break;

  case SkyboxTheme::Music: // リズミカルな色
    outTopColor = {0.5f, 0.4f, 0.7f};
    outHorizonColor = {0.6f, 0.5f, 0.8f};
    outBottomColor = {0.45f, 0.35f, 0.65f};
    break;

  case SkyboxTheme::Literature: // クリーム色
    outTopColor = {0.75f, 0.7f, 0.6f};
    outHorizonColor = {0.85f, 0.8f, 0.7f};
    outBottomColor = {0.7f, 0.65f, 0.55f};
    break;

  case SkyboxTheme::Medical: // クリーンな白緑
    outTopColor = {0.75f, 0.8f, 0.75f};
    outHorizonColor = {0.85f, 0.9f, 0.85f};
    outBottomColor = {0.7f, 0.75f, 0.7f};
    break;

  case SkyboxTheme::Food: // 暖色系
    outTopColor = {0.8f, 0.6f, 0.4f};
    outHorizonColor = {0.9f, 0.75f, 0.5f};
    outBottomColor = {0.75f, 0.55f, 0.35f};
    break;

  case SkyboxTheme::Religion: // 神秘的な紫金
    outTopColor = {0.5f, 0.4f, 0.6f};
    outHorizonColor = {0.65f, 0.55f, 0.5f};
    outBottomColor = {0.45f, 0.35f, 0.55f};
    break;

  case SkyboxTheme::War: // 暗いグレー・煙
    outTopColor = {0.35f, 0.35f, 0.35f};
    outHorizonColor = {0.45f, 0.45f, 0.45f};
    outBottomColor = {0.3f, 0.3f, 0.3f};
    break;

  case SkyboxTheme::Fantasy: // 魔法的
    outTopColor = {0.6f, 0.4f, 0.75f};
    outHorizonColor = {0.7f, 0.6f, 0.85f};
    outBottomColor = {0.55f, 0.35f, 0.7f};
    break;

  case SkyboxTheme::Horror: // 不気味な暗紫
    outTopColor = {0.25f, 0.15f, 0.3f};
    outHorizonColor = {0.35f, 0.2f, 0.35f};
    outBottomColor = {0.2f, 0.1f, 0.25f};
    break;

  case SkyboxTheme::SciFi: // ネオンカラー
    outTopColor = {0.2f, 0.5f, 0.7f};
    outHorizonColor = {0.3f, 0.6f, 0.8f};
    outBottomColor = {0.15f, 0.45f, 0.65f};
    break;

  case SkyboxTheme::Retro: // ヴィンテージ
    outTopColor = {0.6f, 0.55f, 0.45f};
    outHorizonColor = {0.7f, 0.65f, 0.55f};
    outBottomColor = {0.55f, 0.5f, 0.4f};
    break;

  case SkyboxTheme::GolfCourseClear:
    outTopColor     = {0.10f, 0.32f, 0.72f};  // 深コバルトブルー（天頂）
    outHorizonColor = {0.62f, 0.78f, 0.92f};  // 明るい空色（地平線）
    outBottomColor  = {0.42f, 0.60f, 0.75f};  // 地平線以下（反射空）
    break;

  default:
    outTopColor = {0.4f, 0.6f, 0.9f};
    outHorizonColor = {0.7f, 0.8f, 0.95f};
    outBottomColor = {0.6f, 0.7f, 0.85f};
    break;
  }
}

} // namespace graphics

