#pragma once
/**
 * @file WikiLinkGraph.h
 * @brief 記事リンクグラフ（*.linkgraph）の読み込みと、ゴールまでの手数の計算
 *
 * ファイルは tools/linkgraph/build_linkgraph.py が作ります（形式は同スクリプト冒頭）。
 * ゲームが知りたいのは「ある記事からゴールまで最短何手か」だけなので、
 * 各記事の入リンク一覧だけを持ち、ゴールから逆向きに幅優先探索（BFS）を1回すれば
 * 全記事の手数が出ます。
 *
 * - 読み込み後は変更しないため、複数スレッドから同時に読んでかまいません。
 * - 記事は 0 始まりの連番（記事番号）で扱い、Wikipedia の page_id とは
 *   WikiPageId() / FromWikiPageId() で相互に変換します。
 * - タイトルは Wikipedia の表記（空白はアンダースコア）。転送ページのタイトルでも
 *   転送先の記事が引けます。
*/

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game::systems {

class WikiLinkGraph {
public:
  /** @brief 同梱ファイルの既定の場所*/
  static constexpr const char *kDefaultPath = "Assets/data/jawiki.linkgraph";
  /** @brief 届かない記事の手数*/
  static constexpr std::uint8_t kUnreachable = 0xFF;
  /** @brief 記事番号の無効値*/
  static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

  /** @brief ゴール1つ分の逆向き BFS の結果*/
  struct Distances {
    std::uint32_t target = kInvalid;
    /** 記事番号 → ゴールまでの手数（届かなければ kUnreachable）*/
    std::vector<std::uint8_t> hops;
    /** 記事番号 → ゴールへ1手近づく次の記事（ゴール自身と未到達は kInvalid）*/
    std::vector<std::uint32_t> next;
    std::size_t reached = 0;
    bool cancelled = false;
  };

  /**
   * @brief ファイルを読み込みます。
   * @param error 失敗理由を書き込む先（nullptr 可）
   * @param cancel true になったら読み込みを打ち切る（nullptr 可）
   * @return 失敗時は nullptr
  */
  static std::shared_ptr<const WikiLinkGraph>
  Load(const std::string &path, std::string *error = nullptr,
       const std::atomic<bool> *cancel = nullptr);

  /**
   * @brief プロセス内で共有するグラフを返します（初回だけ読み込み）。
   * @details 同じパスなら2回目以降は読み込み済みのものを返します。
   *          読み込みに失敗した場合は次の呼び出しで再試行します。
  */
  static std::shared_ptr<const WikiLinkGraph>
  Shared(const std::string &path = kDefaultPath,
         const std::atomic<bool> *cancel = nullptr);

  /** @brief 共有中のグラフを手放します（次の Shared() で読み直し）。*/
  static void ReleaseShared();

  std::uint32_t ArticleCount() const {
    return static_cast<std::uint32_t>(m_wikiIds.size());
  }
  std::uint64_t LinkCount() const { return m_linkCount; }
  const std::string &SourceName() const { return m_source; }

  /**
   * @brief タイトル（転送ページ名を含む）を完全一致で引きます。
   * @param title Wikipedia の表記（空白はアンダースコア）
   * @return 記事番号（無ければ kInvalid）
  */
  std::uint32_t FindTitle(std::string_view title) const;

  /** @brief 記事の本来のタイトル（空白はアンダースコア）*/
  std::string Title(std::uint32_t article) const;

  std::uint32_t WikiPageId(std::uint32_t article) const {
    return article < m_wikiIds.size() ? m_wikiIds[article] : kInvalid;
  }
  /** @return 記事番号（無ければ kInvalid）*/
  std::uint32_t FromWikiPageId(std::uint32_t wikiPageId) const;

  /** @brief 被リンク数の多い順の記事（被リンク数がファイルの下限以上のもの）*/
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> &Popular() const {
    return m_popular;
  }
  std::uint32_t IncomingCount(std::uint32_t article) const;

  /** @brief 記事へリンクしている記事を昇順に fn(記事番号) で渡します。*/
  template <class Fn>
  void ForEachIncoming(std::uint32_t article, Fn &&fn) const {
    const std::uint8_t *p = m_inLists.data() + m_rowStart[article];
    const std::uint8_t *end = m_inLists.data() + m_rowStart[article + 1];
    std::uint32_t prev = kInvalid; // -1 からの差
    while (p < end) {
      std::uint32_t gap = 0;
      int shift = 0;
      std::uint8_t byte;
      do {
        byte = *p++;
        gap |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
        shift += 7;
      } while (byte & 0x80);
      prev += gap + 1;
      fn(prev);
    }
  }

  /**
   * @brief ゴールから逆向きに BFS して、全記事の手数を求めます。
   * @param target ゴールの記事番号
   * @param blocked 通さない記事（記事番号ごとに 0/1。空なら無し）。ゴール自身は通す
   * @param threads 使うスレッド数（0 なら自動）
   * @param cancel true になったら打ち切る（nullptr 可）
  */
  std::shared_ptr<Distances>
  ComputeDistances(std::uint32_t target,
                   const std::vector<std::uint8_t> &blocked,
                   unsigned threads = 0,
                   const std::atomic<bool> *cancel = nullptr) const;

  /** @brief 読み込み済みデータのおおよそのメモリ使用量（バイト）*/
  std::size_t MemoryBytes() const;

private:
  WikiLinkGraph() = default;

  /** @brief i 番目の見出しを（ブロック先頭から復元して）返します。*/
  std::string TitleEntry(std::uint32_t entry) const;

  std::string m_source;
  std::uint64_t m_linkCount = 0;
  std::vector<std::uint8_t> m_inLists;
  std::vector<std::uint32_t> m_rowStart; // 記事数 + 1
  std::vector<std::uint32_t> m_wikiIds;  // 記事番号 → page_id
  std::vector<std::pair<std::uint32_t, std::uint32_t>> m_wikiSorted; // (page_id, 記事番号)
  std::vector<std::pair<std::uint32_t, std::uint32_t>> m_popular; // (記事番号, 被リンク数)
  std::string m_titles;                  // 前方圧縮した見出しの列
  std::vector<std::uint32_t> m_blockOffset; // 64件ごとのブロックの開始位置
  std::vector<std::uint32_t> m_titleIds;   // 見出し → 記事番号
  std::vector<std::uint32_t> m_ownTitle;   // 記事番号 → 本来のタイトルの見出し番号
};

} // namespace game::systems
