#pragma once
/**
 * @file WikiShortestPath.h
 * @brief 日本語Wikipedia最短経路計算（SDOW統合）
 *
 * jawiki_sdowで生成されたSQLiteデータベースを使用して
 * 2記事間の最短リンク数を計算する。
 */

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 前方宣言
struct sqlite3;

namespace game::systems {

/**
 * @brief 最短経路計算結果
 */
struct ShortestPathResult {
  bool success = false;          ///< 計算成功
  int degrees = -1;              ///< 最短リンク数（-1=失敗）
  std::vector<std::string> path; ///< 経路（タイトルリスト）
  std::string errorMessage;      ///< エラーメッセージ
};

/**
 * @brief Wikipedia最短経路計算クラス
 *
 * 双方向BFSで効率的に最短経路を探索
 */
class WikiShortestPath {
public:
  WikiShortestPath() = default;
  ~WikiShortestPath();

  // コピー禁止
  WikiShortestPath(const WikiShortestPath &) = delete;
  WikiShortestPath &operator=(const WikiShortestPath &) = delete;

  /**
   * @brief データベース初期化
   * @param dbPath SQLiteデータベースパス
   * @return 成功ならtrue
   */
  bool Initialize(const std::string &dbPath, bool cachePopularPages = true);

  /**
   * @brief データベースが利用可能か
   */
  bool IsAvailable() const { return m_db != nullptr; }

  /**
   * @brief 記事タイトルからページIDを解決します。 山内陽
   * @param title 記事タイトル
   * @return 解決できたページID（失敗時は-1）
   */
  int ResolvePageId(const std::string &title);

  /**
   * @brief 最短経路を計算
   * @param sourceTitle 開始記事タイトル
   * @param targetTitle 目標記事タイトル
   * @param maxDepth 最大探索深度（デフォルト4）
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
   * @param maxDepth 最大探索深度（デフォルト4）
   * @param logSuccess 成功時の経路ログを出すか
   * @return 計算結果
   */
  ShortestPathResult FindShortestPath(const std::string &sourceTitle,
                                      int targetId, int maxDepth = 4,
                                      bool logSuccess = true);

  /**
   * @brief 複数記事からターゲットまでの最短リンク数を一括計算します。 山内陽
   * @param sourceTitles 開始記事タイトル一覧
   * @param targetId 目標記事ID
   * @param maxDepth 最大探索深度
   * @param progressUnits 進捗の完了単位数を書き込む先
   * @param progressBase この計算の開始前に完了済みとして扱う単位数
   * @return タイトルごとのリンク数（未到達・未登録記事は含めない）
   */
  std::unordered_map<std::string, int>
  ComputeDistancesToTarget(const std::vector<std::string> &sourceTitles,
                           int targetId, int maxDepth = 4,
                           std::atomic<size_t> *progressUnits = nullptr,
                           size_t progressBase = 0);

private:
  /// @brief タイトルからページIDを取得
  int FetchPageId(const std::string &title);

  /// @brief ページIDからタイトルを取得
  std::string FetchPageTitle(int pageId);

  /// @brief 出力リンクを取得（パイプ区切り文字列）
  std::string FetchOutgoingLinks(int pageId);

  /// @brief 入力リンクを取得（パイプ区切り文字列）
  std::string FetchIncomingLinks(int pageId);

  /// @brief 出力リンク総数を取得
  int FetchOutgoingLinksCount(const std::vector<int> &pageIds);

  /// @brief 入力リンク総数を取得
  int FetchIncomingLinksCount(const std::vector<int> &pageIds);

  /// @brief パスを再構築
  std::vector<std::vector<int>> ReconstructPaths(
      const std::vector<int> &pageIds,
      const std::unordered_map<int, std::vector<int>> &visitedDict);

public:
  /**
   * @brief 入力リンク数が一定以上の人気記事をランダム取得
   * @param minIncomingLinks 最小入力リンク数（デフォルト100）
   * @return 記事タイトル（失敗時は空文字列）
   */
  std::pair<std::string, int> FetchPopularPageTitle(int minIncomingLinks = 100);

private:
  sqlite3 *m_db = nullptr;
  std::vector<int> m_popularPageIds; ///< 人気記事IDのキャッシュ
};

} // namespace game::systems
