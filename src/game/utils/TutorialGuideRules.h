#pragma once
/**
 * @file TutorialGuideRules.h
 * @brief チュートリアルの案内パネルに出す内容と、進み具合の数え方
 * @details 描画やECSに依存しない純粋な規則だけを置く。文言はすべてここで決める。
*/

#include "../controllers/TutorialStep.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace game::utils {

/** @brief キー表示どうしのつなぎ方*/
enum class TutorialKeyJoiner {
  None, ///< つなぎなし（行の先頭）
  Then, ///< 続けて押す（→）
  Plus, ///< 両方行う（＋）
  Or    ///< どちらでもよい（または）
};

/** @brief キーやマウス操作を1つ表す表示*/
struct TutorialKeyChip {
  std::wstring key;     ///< キーの名前（例: 左クリック、Q）
  std::wstring caption; ///< その操作で起きること（例: 回す）。空でもよい
  TutorialKeyJoiner joiner = TutorialKeyJoiner::None; ///< 直前の表示とのつなぎ方

  bool operator==(const TutorialKeyChip &) const = default;
};

/** @brief 枠で囲んで示すHUD部品*/
enum class TutorialSpotlight {
  None,
  Minimap,   ///< 右のミニマップ
  ClubPanel, ///< 左下のクラブ表示
  ShotRuler  ///< 下の定規ゲージ（スイング中だけ示す）
};

/** @brief 案内パネル1画面ぶんの内容*/
struct TutorialGuideContent {
  int chapterIndex = 0;       ///< 章の番号（0始まり）
  std::wstring chapterTitle;  ///< 章の名前
  int pageInChapter = 1;      ///< 章の中で何画面目か（1始まり）
  int pagesInChapter = 1;     ///< 章の画面数
  std::wstring headline;      ///< いますること（最も大きく出す）
  std::wstring detail;        ///< 補足の説明
  std::vector<TutorialKeyChip> keys; ///< 使うキーやマウス操作
  std::wstring hint;          ///< スキップなどの案内
  TutorialSpotlight spotlight = TutorialSpotlight::None;
  bool celebrate = false;     ///< 達成を祝う表示にするか

  bool operator==(const TutorialGuideContent &) const = default;
};

/** @brief 内容を決めるための入力*/
struct TutorialGuideState {
  game::controllers::TutorialStep step = game::controllers::TutorialStep::Intro;
  std::size_t cardIndex = 0;        ///< 地形・旗の説明で何枚目か（0始まり）
  std::size_t terrainCardCount = 0; ///< 地形の説明の枚数
  std::size_t flagCardCount = 0;    ///< 旗の説明の枚数
  std::wstring cardName;            ///< 説明中の対象の名前
  std::wstring cardDescription;     ///< 説明中の対象の説明
  bool goalCleared = false;         ///< ゴールに入れた後か
  std::wstring wrongCupName; ///< 違う旗に入ったとき、その旗の名前（空なら出さない）
};

inline constexpr int kTutorialChapterCount = 7;

/** @brief 章の名前を返します。*/
inline const wchar_t *GetTutorialChapterTitle(int chapterIndex) {
  constexpr std::array<const wchar_t *, kTutorialChapterCount> kTitles = {
      L"はじめに", L"カメラ", L"ねらう", L"打つ",
      L"コースを知る", L"マップ", L"実戦"};
  if (chapterIndex < 0 || chapterIndex >= kTutorialChapterCount) {
    return L"";
  }
  return kTitles[static_cast<std::size_t>(chapterIndex)];
}

/** @brief 段階が属する章の番号を返します。*/
inline int GetTutorialChapterIndex(game::controllers::TutorialStep step) {
  using game::controllers::TutorialStep;
  switch (step) {
  case TutorialStep::Intro:
    return 0;
  case TutorialStep::Camera:
    return 1;
  case TutorialStep::Club:
  case TutorialStep::Aim:
    return 2;
  case TutorialStep::Power:
  case TutorialStep::Impact:
    return 3;
  case TutorialStep::TerrainInfo:
  case TutorialStep::FlagInfo:
    return 4;
  case TutorialStep::MapOpen:
  case TutorialStep::MapPan:
  case TutorialStep::MapZoom:
  case TutorialStep::MapAim:
  case TutorialStep::MapHelp:
  case TutorialStep::MapClose:
    return 5;
  case TutorialStep::LinkCup:
  case TutorialStep::GoalCup:
  case TutorialStep::Done:
    return 6;
  }
  return 0;
}

/**
 * @brief 章の中で何画面目か、章が何画面あるかを求めます。
 * @param outPage 1始まりの画面番号
 * @param outPages 章の画面数
*/
inline void GetTutorialPageInChapter(const TutorialGuideState &state,
                                     int &outPage, int &outPages) {
  using game::controllers::TutorialStep;
  const int stepValue = static_cast<int>(state.step);
  const int terrainCards = static_cast<int>(state.terrainCardCount);
  const int flagCards = static_cast<int>(state.flagCardCount);
  const int card = static_cast<int>(state.cardIndex);
  outPage = 1;
  outPages = 1;
  switch (state.step) {
  case TutorialStep::Club:
  case TutorialStep::Power:
  case TutorialStep::LinkCup:
    outPages = 2;
    break;
  case TutorialStep::Aim:
  case TutorialStep::Impact:
  case TutorialStep::GoalCup:
  case TutorialStep::Done:
    outPage = 2;
    outPages = 2;
    break;
  case TutorialStep::TerrainInfo:
    outPages = std::max(1, terrainCards + flagCards);
    outPage = std::clamp(card + 1, 1, outPages);
    break;
  case TutorialStep::FlagInfo:
    outPages = std::max(1, terrainCards + flagCards);
    outPage = std::clamp(terrainCards + card + 1, 1, outPages);
    break;
  case TutorialStep::MapOpen:
  case TutorialStep::MapPan:
  case TutorialStep::MapZoom:
  case TutorialStep::MapAim:
  case TutorialStep::MapHelp:
  case TutorialStep::MapClose:
    outPages = static_cast<int>(TutorialStep::MapClose) -
               static_cast<int>(TutorialStep::MapOpen) + 1;
    outPage = stepValue - static_cast<int>(TutorialStep::MapOpen) + 1;
    break;
  default:
    break;
  }
}

/**
 * @brief 進行バーの1区画をどこまで塗るかを返します（0.0〜1.0）。
 * @details 終わった章は1、これからの章は0。いまの章は画面の進みに応じて塗り、
 *          1画面目でも少し塗って現在地が分かるようにします。
*/
inline float GetTutorialChapterFill(int chapterIndex, int currentChapter,
                                    int pageInChapter, int pagesInChapter) {
  if (chapterIndex < currentChapter) {
    return 1.0f;
  }
  if (chapterIndex > currentChapter) {
    return 0.0f;
  }
  const float pages = static_cast<float>(std::max(1, pagesInChapter));
  const float page = static_cast<float>(std::clamp(pageInChapter, 1,
                                                   std::max(1, pagesInChapter)));
  return (page - 0.5f) / pages;
}

/** @brief 枠で示すHUD部品の呼び名を返します（目次の「見る場所」に出す）。*/
inline const wchar_t *GetTutorialSpotlightLabel(TutorialSpotlight spotlight) {
  switch (spotlight) {
  case TutorialSpotlight::Minimap:
    return L"ミニマップ";
  case TutorialSpotlight::ClubPanel:
    return L"左下のクラブ表示";
  case TutorialSpotlight::ShotRuler:
    return L"下の定規ゲージ";
  default:
    return L"";
  }
}

/** @brief 目次の節番号（「3.2」のように章番号.画面番号、どちらも1始まり）を返します。*/
inline std::wstring GetTutorialSectionNumber(int chapterIndex,
                                             int pageInChapter) {
  return std::to_wstring(chapterIndex + 1) + L"." +
         std::to_wstring(std::max(1, pageInChapter));
}

/**
 * @brief 目次に並べる章の範囲を返します。
 * @param compact 縦に収まらないとき true。いまの章と前後1章だけにする
 * @param outFirst 最初に出す章（0始まり）
 * @param outLast 最後に出す章（0始まり、含む）
*/
inline void GetTutorialTocRange(int currentChapter, bool compact,
                                int &outFirst, int &outLast) {
  const int current = std::clamp(currentChapter, 0, kTutorialChapterCount - 1);
  if (!compact) {
    outFirst = 0;
    outLast = kTutorialChapterCount - 1;
    return;
  }
  outFirst = std::max(0, current - 1);
  outLast = std::min(kTutorialChapterCount - 1, current + 1);
}

/** @brief 現在の段階に出す案内の内容を組み立てます。*/
inline TutorialGuideContent BuildTutorialGuideContent(
    const TutorialGuideState &state) {
  using game::controllers::TutorialStep;
  using Joiner = TutorialKeyJoiner;

  TutorialGuideContent content;
  content.chapterIndex = GetTutorialChapterIndex(state.step);
  content.chapterTitle = GetTutorialChapterTitle(content.chapterIndex);
  GetTutorialPageInChapter(state, content.pageInChapter,
                           content.pagesInChapter);

  const wchar_t *const kSkipPractice = L"Enter：この練習をとばす";
  const wchar_t *const kSkipMap = L"Enter：マップの説明をとばす";

  switch (state.step) {
  case TutorialStep::Intro:
    content.headline = L"旗に入れて、次の記事へ進もう";
    content.detail = L"記事の中のリンクが、旗の立ったカップになっています。"
                     L"入れると、その記事が次のコースになります。";
    content.keys = {{L"Enter", L"はじめる"}};
    break;
  case TutorialStep::Camera:
    content.headline = L"まわりを見回そう";
    content.detail = L"左ドラッグで向きを回すと次へ進みます。"
                     L"Shift を押している間は、ゆっくり動きます。";
    content.keys = {{L"左ドラッグ", L"回す"}, {L"ホイール", L"寄る・引く"}};
    content.hint = kSkipPractice;
    break;
  case TutorialStep::Aim:
    content.headline = L"ねらう場所にピンを置こう";
    content.detail = L"右のミニマップか、コースの上で押します。"
                     L"距離に合うクラブに自動で替わります。";
    content.keys = {{L"中クリック", L"ホイールを押し込む"}};
    content.hint = kSkipPractice;
    content.spotlight = TutorialSpotlight::Minimap;
    break;
  case TutorialStep::Club:
    content.headline = L"クラブを替えてみよう";
    content.detail = L"クラブごとに、飛ぶ距離と高さが変わります。";
    content.keys = {{L"Q", L""}, {L"E", L"前後のクラブへ", Joiner::Or}};
    content.hint = kSkipPractice;
    content.spotlight = TutorialSpotlight::ClubPanel;
    break;
  case TutorialStep::Power:
    content.headline = L"強さを決めよう";
    content.detail = L"のびていく線を、ピンの印の近くで止めます。"
                     L"右クリックでやり直せます。";
    content.keys = {{L"左クリック", L"始める"},
                    {L"左クリック", L"止める", Joiner::Then}};
    content.hint = kSkipPractice;
    content.spotlight = TutorialSpotlight::ShotRuler;
    break;
  case TutorialStep::Impact:
    content.headline = L"タイミングを合わせて打とう";
    content.detail = L"動く針が、いま決めた強さの位置に重なったら押します。"
                     L"近いほど、ねらいどおりに飛びます。";
    content.keys = {{L"左クリック", L"打つ"}};
    content.hint = L"右クリック：強さからやり直す　Enter：とばす";
    content.spotlight = TutorialSpotlight::ShotRuler;
    break;
  case TutorialStep::TerrainInfo:
  case TutorialStep::FlagInfo:
    if (!state.cardName.empty()) {
      content.headline = state.cardName;
      content.detail = state.cardDescription;
    } else if (state.step == TutorialStep::TerrainInfo) {
      content.headline = L"地形のちがい";
      content.detail = L"地形によって、転がりやすさと打ちやすさが変わります。";
    } else {
      content.headline = L"旗の色";
      content.detail = L"旗の色は、ゴールの記事までの近さを表します。";
    }
    content.keys = {{L"Enter", L"次へ"}};
    break;
  case TutorialStep::MapOpen:
    content.headline = L"全体マップを開こう";
    content.detail = L"コース全体と、すべての旗の位置を上から見られます。";
    content.keys = {{L"M", L"開く"}};
    content.hint = kSkipMap;
    content.spotlight = TutorialSpotlight::Minimap;
    break;
  case TutorialStep::MapPan:
    content.headline = L"マップを動かそう";
    content.detail = L"左右どちらのボタンでも動かせます。";
    content.keys = {{L"ドラッグ", L"動かす"}};
    content.hint = kSkipMap;
    break;
  case TutorialStep::MapZoom:
    content.headline = L"拡大・縮小しよう";
    content.detail = L"旗が集まっている場所を見るときに便利です。";
    content.keys = {{L"ホイール", L"拡大・縮小"}};
    content.hint = kSkipMap;
    break;
  case TutorialStep::MapAim:
    content.headline = L"マップの上にピンを置こう";
    content.detail = L"遠くの旗をねらうときは、マップから置くと正確です。";
    content.keys = {{L"中クリック", L"ピンを置く"}};
    content.hint = kSkipMap;
    break;
  case TutorialStep::MapHelp:
    content.headline = L"操作の一覧を開いて閉じよう";
    content.detail = L"忘れたときは、いつでも確認できます。"
                     L"Space でボール、F でコース全体を映します。";
    content.keys = {{L"?", L"開く"}, {L"?", L"閉じる", Joiner::Then}};
    content.hint = kSkipMap;
    break;
  case TutorialStep::MapClose:
    content.headline = L"マップを閉じよう";
    content.detail = L"ショットの画面へ戻ります。";
    content.keys = {{L"M", L""}, {L"Esc", L"閉じる", Joiner::Or}};
    content.hint = kSkipMap;
    break;
  case TutorialStep::LinkCup:
    content.headline = L"目の前の旗に入れよう";
    content.detail = L"向きとピンは合わせてあります。"
                     L"入れると「フェアウェイ」の記事へ進みます。";
    content.keys = {{L"左クリック", L"始める"},
                    {L"左クリック", L"強さ", Joiner::Then},
                    {L"左クリック", L"打つ", Joiner::Then}};
    content.hint = L"Enter：とばして次へ";
    break;
  case TutorialStep::GoalCup:
    if (state.goalCleared) {
      content.headline = L"ゴール！ おつかれさまでした";
      content.detail = L"このあと接続を確認して、最初のコースを始めます。";
      content.celebrate = true;
    } else {
      content.headline = L"赤い旗がゴールです";
      content.detail = L"正面の奥にあります。何打かけてもかまいません。"
                       L"ほかの旗に入ると、最初の場所へ戻ります。";
      content.keys = {{L"中クリック", L"ピンを置く"},
                      {L"左クリック", L"打つ", Joiner::Then}};
      content.hint = L"Esc：ポーズ・チュートリアルを終える";
    }
    break;
  case TutorialStep::Done:
    content.headline = L"ゴール！ おつかれさまでした";
    content.detail = L"Wikipediaへの接続を確認しています。"
                     L"確認できたら、最初のコースを始めます。";
    content.celebrate = true;
    break;
  }
  // 違う旗に入ったときは、補足の代わりに理由を出す。
  if (!state.wrongCupName.empty() && !state.goalCleared &&
      (state.step == TutorialStep::LinkCup ||
       state.step == TutorialStep::GoalCup)) {
    const wchar_t *const expected = state.step == TutorialStep::LinkCup
                                        ? L"「フェアウェイ」の旗"
                                        : L"赤い旗";
    content.detail = L"「" + state.wrongCupName + L"」の旗でした。"
                     L"ここでは" + expected + L"に入れます。"
                     L"最初の場所から打ち直しです。";
  }
  return content;
}

} // namespace game::utils
