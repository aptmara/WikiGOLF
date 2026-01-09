#pragma once
/**
 * @file WikiClient.h
 * @brief Wikipedia APIと通信するためのクライアント
 */

#include <functional>
#include <string>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace game::systems {

/**
 * @brief 内部リンク情報
 */
struct WikiLink {
  std::string title;   ///< リンク先記事タイトル
  std::string display; ///< 表示テキスト
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

  /**
   * @brief 指定した記事の内部リンク一覧を取得します
   * @param title 記事タイトル
   * @param limit 取得上限（デフォルト50）
   * @return 内部リンクのリスト
   */
  std::vector<WikiLink> FetchPageLinks(const std::string &title,
                                       int limit = 50);

  /**
   * @brief 目的記事用のランダムな「人気記事」を取得します
   * @return 記事タイトル
   */
  std::string FetchTargetPageTitle();

  /**
   * @brief 記事の冒頭（リード文）を取得します
   * @param title 記事タイトル
   * @param lengthLimit 文字数制限（概算）
   * @return 記事のリード文
   */
  std::string FetchPageExtract(const std::string &title, int lengthLimit = 200);

  /**
   * @brief URLエンコードを行います
   * @param str エンコード対象文字列
   * @return エンコード済み文字列
   */
  static std::string UrlEncode(const std::string &str);

private:
  std::string PerformGetRequest(const std::wstring &server,
                                const std::wstring &path);

  HINTERNET m_hSession = nullptr;
  HINTERNET m_hConnect = nullptr;
};

} // namespace game::systems
