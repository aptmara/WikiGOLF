#pragma once
/**
 * @file WikiClient.h
 * @brief Wikipedia APIと通信するためのクライアント
 */

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace game {
/**
 * @brief 内部リンク情報
 */
struct WikiLink {
  std::string title;   ///< リンク先記事タイトル
  std::string display; ///< 表示テキスト
};
} // namespace game

namespace game::systems {

/**
 * @brief 記事内に埋め込まれた画像1件分の情報（REST page/media-list より取得）
 */
struct WikiImageInfo {
  std::string fileTitle;      ///< ファイル名（"ファイル:xxx.jpg"）
  bool leadImage = false;     ///< 記事先頭（インフォボックス相当）の代表画像か
  int sectionId = -1;         ///< 属する節番号（tocdataのindexと対応。0=リード文）
  std::string caption;        ///< キャプション（プレーンテキスト）
  std::string thumbUrl;       ///< サムネイル画像のダウンロードURL（https補完済み）
};

/**
 * @brief 記事の節（見出し）1件分の情報（prop=tocdata より取得）
 */
struct WikiSectionInfo {
  int index = -1;       ///< 節番号（WikiImageInfo::sectionIdと対応）
  int level = 2;        ///< 見出しレベル（2=H2, 3=H3...）
  std::string heading;  ///< 見出しテキスト（プレーンテキスト化済み）
};

/**
 * @brief ランダムに選ばれた目標記事の情報（タイトル＋代表サムネイル）
 */
struct WikiTargetPage {
  std::string title;
  std::string thumbnailUrl; ///< 代表サムネイルURL（取得できなければ空文字列）
};

class WikiClient {
public:
  WikiClient();
  ~WikiClient();

  /**
   * @brief ランダムなWikipedia記事のタイトルを取得します
   * @return 記事タイトル（失敗時は空文字列）
   */
  std::string FetchRandomPageTitle();

  /// @brief 記事のリンク一覧を取得
  /// @param title 記事タイトル
  /// @param limit 最大取得数
  /// @return リンク情報のリスト
  std::vector<game::WikiLink> FetchPageLinks(const std::string &title,
                                             int limit = 50);

  /// @brief 記事のカテゴリ一覧を取得
  /// @param title 記事タイトル
  /// @return カテゴリ名（"Category:xxx"）のリスト
  std::vector<std::string> FetchPageCategories(const std::string &title);

  /// @brief 目標ページ（ゴール）をランダムに決定
  /// @return ページタイトル
  std::string FetchTargetPageTitle();

  /// @brief 目標ページ（ゴール）をランダムに決定し、代表サムネイルURLも同時に取得します
  ///        （generator=random + prop=pageimages を1リクエストにまとめる）
  /// @param thumbSize サムネイルの目安幅（px）
  /// @return タイトルとサムネイルURL
  WikiTargetPage FetchTargetPage(int thumbSize = 256);

  /// @brief 指定タイトルの記事代表サムネイルURLを取得します（prop=pageimages）
  /// @param title 記事タイトル
  /// @param thumbSize サムネイルの目安幅（px）
  /// @return サムネイルURL（取得できなければ空文字列）
  std::string FetchPageThumbnail(const std::string &title, int thumbSize = 256);

  /**
   * @brief 記事の全文（プレーンテキスト）を取得します
   * @param title 記事タイトル
   * @param lengthLimit (現在は使用されません。互換性のために残されています)
   * @return 記事の全文
   */
  std::string FetchPageExtract(const std::string &title, int lengthLimit = 0);

  /**
   * @brief 記事に埋め込まれた画像一覧を取得します（UIアイコン等は除外済み）
   * @param title 記事タイトル
   * @param maxImages 取得する最大件数
   * @return 画像情報のリスト（記事内での出現順）
   */
  std::vector<WikiImageInfo> FetchPageImages(const std::string &title,
                                             int maxImages = 6);

  /// @brief 記事の節（見出し）一覧を取得します
  /// @param title 記事タイトル
  /// @return 節情報のリスト
  std::vector<WikiSectionInfo> FetchPageSections(const std::string &title);

  /// @brief 任意URLからバイナリデータ（画像等）をダウンロードします
  /// @param url ダウンロード対象URL（https://から始まる想定）
  /// @return バイナリデータ（失敗時は空文字列）
  std::string DownloadBinary(const std::string &url);

  /**
   * @brief URLエンコードを行います
   * @param str エンコード対象文字列
   * @return エンコード済み文字列
   */
  static std::string UrlEncode(const std::string &str);

private:
  std::string PerformGetRequest(const std::wstring &server,
                                const std::wstring &path);

  /// @brief 指定ホストへの接続を取得する（未接続なら新規に確立してキャッシュする）
  HINTERNET GetOrCreateConnection(const std::wstring &server);

  HINTERNET m_hSession = nullptr;
  HINTERNET m_hConnect = nullptr; ///< ja.wikipedia.org への既定接続
  std::unordered_map<std::wstring, HINTERNET> m_hostConnections; ///< 他ホスト用の接続キャッシュ
};

} // namespace game::systems
