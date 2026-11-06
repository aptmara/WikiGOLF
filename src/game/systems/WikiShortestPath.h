#pragma once
/**
 * @file WikiShortestPath.h
 * @brief 日本語Wikipediaの記事間の最短リンク数
 *
 * 同梱のリンクグラフ（Assets/data/jawiki.linkgraph、WikiLinkGraph）を使います。
 * グラフはプロセス内で1つだけ読み込んで共有し、ゴールごとに逆向き BFS で
 * 全記事の手数表を作ってキャッシュします。以後の問い合わせは表を引くだけです。
 *
 * ページIDは Wikipedia の page_id です（転送ページは転送先の記事のID）。
*/

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game::systems {

class WikiLinkGraph;

/**
 * @brief 最短経路計算結果
*/
struct ShortestPathResult {
  bool success = false;          /**< 計算成功*/
  int degrees = -1;              /**< 最短リンク数（-1=失敗）*/
  std::vector<std::string> path; /**< 経路（タイトルリスト）*/
  std::string errorMessage;      /**< エラーメッセージ*/
};

/**
 * @brief Wikipedia最短経路計算クラス
*/
class WikiShortestPath {
public:
  /** @brief 同梱のリンクグラフの場所*/
  static constexpr const char *kDefaultGraphPath = "Assets/data/jawiki.linkgraph";

  WikiShortestPath() = default;
  ~WikiShortestPath();

  // コピー禁止
  WikiShortestPath(const WikiShortestPath &) = delete;
  WikiShortestPath &operator=(const WikiShortestPath &) = delete;

  /**
   * @brief リンクグラフを使えるようにします（初回だけ読み込み、以後は共有）。
   * @param graphPath リンクグラフのパス
   * @param cachePopularPages 人気記事の候補を用意するか
   * @return 成功ならtrue
  */
  bool Initialize(const std::string &graphPath = kDefaultGraphPath,
                  bool cachePopularPages = true);

  /**
   * @brief データベースが利用可能か
  */
  bool IsAvailable() const { return m_graph != nullptr; }

  /**
   * @brief 記事タイトルからページIDを解決します。
   * @param title 記事タイトル
   * @return 解決できたページID（失敗時は-1）
  */
  int ResolvePageId(const std::string &title);

  /**
   * @brief 最短経路を計算
   * @param sourceTitle 開始記事タイトル
   * @param targetTitle 目標記事タイトル
   * @param maxDepth これより遠ければ「見つからない」として扱う手数
   * @param logSuccess 成功時の経路ログを出すか
   * @return 計算結果
  */
  ShortestPathResult FindShortestPath(const std::string &sourceTitle,
                                      const std::string &targetTitle,
                                      int maxDepth = 4,
                                      bool logSuccess = true);

  /**
   * @brief 最短経路を計算（ターゲットID指定）
   * @param sourceTitle 開始記事タイトル
   * @param targetId 目標記事ID
   * @param maxDepth これより遠ければ「見つからない」として扱う手数
   * @param logSuccess 成功時の経路ログを出すか
   * @return 計算結果
  */
  ShortestPathResult FindShortestPath(const std::string &sourceTitle,
                                      int targetId, int maxDepth = 4,
                                      bool logSuccess = true);

  /**
   * @brief 複数記事からターゲットまでの最短リンク数を一括計算します。
   * @param sourceTitles 開始記事タイトル一覧
   * @param targetId 目標記事ID
   * @param maxDepth これより遠い記事は結果に含めない
   * @param progressUnits 進捗の完了単位数を書き込む先
   * @param progressBase この計算の開始前に完了済みとして扱う単位数
   * @param onResolved タイトルごとの距離が確定した時に呼ばれる通知です。
   * @param cancelRequested この計算だけを中断するためのフラグ（nullptr可）
   * @return タイトルごとのリンク数（未到達・未登録記事は含めない）
  */
  std::unordered_map<std::string, int>
  ComputeDistancesToTarget(const std::vector<std::string> &sourceTitles,
                           int targetId, int maxDepth = 4,
                           std::atomic<size_t> *progressUnits = nullptr,
                           size_t progressBase = 0,
                           const std::function<void(const std::string&, int)>&
                               onResolved = nullptr,
                           const std::atomic<bool>* cancelRequested = nullptr);

  /**
   * @brief 入力リンク数が一定以上の人気記事をランダム取得
   * @param minIncomingLinks 最小入力リンク数（候補は常に被リンク1万以上の上位）
   * @return 記事タイトルとページID（失敗時は空文字列と-1）
  */
  std::pair<std::string, int> FetchPopularPageTitle(int minIncomingLinks = 100);

  /**
   * @brief 人気記事を指定seedから決定的に取得します。
   * @param minIncomingLinks 最小入力リンク数
   * @param seed 候補選択に使用するseed
   * @return 記事タイトルとページID（失敗時は空文字列と-1）
  */
  std::pair<std::string, int>
  FetchPopularPageTitle(int minIncomingLinks, std::uint32_t seed);

  /**
   * @brief 実行中の読み込み・探索に中断を要求します。
   *
   * ウィンドウが閉じられた際などに呼び出します。以後の Initialize() も
   * 読み込みをせずに失敗します。
  */
  static void RequestCancelAll();

  /**
   * @brief RequestCancelAll() が呼ばれたかどうかを返します。
  */
  static bool IsCancelRequested();

  /**
   * @brief ゴールごとの手数表のキャッシュを破棄します。
   * @details ゲーム終了時（タイトルへ戻る等）やセーフモードの切り替え時に呼びます。
  */
  static void ClearProcessCaches();

private:
  /** @brief タイトルから記事番号（グラフ内の連番）を引く。無ければ kInvalid*/
  std::uint32_t FindArticle(const std::string &title) const;
  /** @brief タイトルからページIDを取得*/
  int FetchPageId(const std::string &title);
  /** @brief 記事番号から表示用タイトル（空白区切り）を取得*/
  std::string ArticleTitle(std::uint32_t article) const;

  std::pair<std::string, int> FetchPopularPageTitleWithIndexSelector(
      const std::function<std::size_t(std::size_t)> &selectIndex);

  std::shared_ptr<const WikiLinkGraph> m_graph;
  std::vector<std::uint32_t> m_popularArticles; /**< 人気記事（記事番号）の候補*/
};

} // namespace game::systems
