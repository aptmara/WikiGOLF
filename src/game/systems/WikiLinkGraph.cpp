/**
 * @file WikiLinkGraph.cpp
 * @brief 記事リンクグラフの読み込みと逆向き BFS
*/
#include "WikiLinkGraph.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace game::systems {

namespace {

struct Section {
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint64_t rawSize = 0;
  std::uint32_t codec = 0;
  std::uint32_t frameSize = 0;
};

template <class T> T ReadLe(const std::uint8_t *p) {
  T value;
  std::memcpy(&value, p, sizeof(T));
  return value;
}

bool Cancelled(const std::atomic<bool> *cancel) {
  return cancel && cancel->load(std::memory_order_relaxed);
}

unsigned DefaultThreads() {
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  // 描画スレッドなどの分を1つ残す
  return std::clamp(hw > 2 ? hw - 1 : hw, 1u, 32u);
}

/** @brief 0..count-1 の仕事を threads 本で分けて fn(i) を呼びます。*/
void ParallelFor(std::size_t count, unsigned threads,
                 const std::function<void(std::size_t)> &fn) {
  threads = static_cast<unsigned>(
      std::min<std::size_t>(std::max(1u, threads), count));
  if (threads <= 1) {
    for (std::size_t i = 0; i < count; ++i) {
      fn(i);
    }
    return;
  }
  std::atomic<std::size_t> nextIndex{0};
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (unsigned t = 0; t < threads; ++t) {
    workers.emplace_back([&] {
      for (std::size_t i = nextIndex.fetch_add(1); i < count;
           i = nextIndex.fetch_add(1)) {
        fn(i);
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
}

/** @brief meta の JSON から数値1つを取り出します（簡易）。*/
std::uint64_t JsonNumber(const std::string &json, const char *key) {
  const std::string needle = std::string("\"") + key + "\":";
  auto pos = json.find(needle);
  if (pos == std::string::npos) {
    return 0;
  }
  pos += needle.size();
  while (pos < json.size() && json[pos] == ' ') {
    ++pos;
  }
  std::uint64_t value = 0;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    value = value * 10 + static_cast<std::uint64_t>(json[pos] - '0');
    ++pos;
  }
  return value;
}

std::string JsonString(const std::string &json, const char *key) {
  const std::string needle = std::string("\"") + key + "\":";
  auto pos = json.find(needle);
  if (pos == std::string::npos) {
    return {};
  }
  pos = json.find('"', pos + needle.size());
  if (pos == std::string::npos) {
    return {};
  }
  const auto end = json.find('"', pos + 1);
  return end == std::string::npos ? std::string{} : json.substr(pos + 1, end - pos - 1);
}

constexpr std::uint32_t kTitleBlock = 64;

} // namespace

std::shared_ptr<const WikiLinkGraph>
WikiLinkGraph::Load(const std::string &path, std::string *error,
                    const std::atomic<bool> *cancel) {
  const auto fail = [&](std::string message) -> std::shared_ptr<const WikiLinkGraph> {
    if (error) {
      *error = std::move(message);
    }
    return nullptr;
  };

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return fail("ファイルを開けません: " + path);
  }
  const auto fileSize = static_cast<std::uint64_t>(file.tellg());
  if (fileSize < 32) {
    return fail("ファイルが短すぎます");
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(data.data()),
                 static_cast<std::streamsize>(fileSize))) {
    return fail("ファイルを読めません");
  }
  file.close();
  if (Cancelled(cancel)) {
    return fail("中断しました");
  }

  if (std::memcmp(data.data(), "WGLG", 4) != 0 ||
      ReadLe<std::uint32_t>(data.data() + 4) != 2) {
    return fail("形式が違います");
  }
  const std::uint32_t sectionCount = ReadLe<std::uint32_t>(data.data() + 8);
  if (ReadLe<std::uint64_t>(data.data() + 16) != fileSize ||
      32ull + 48ull * sectionCount > fileSize) {
    return fail("ファイルの長さが合いません（壊れているか途中までです）");
  }
  std::map<std::string, Section> sections;
  for (std::uint32_t i = 0; i < sectionCount; ++i) {
    const std::uint8_t *entry = data.data() + 32 + 48ull * i;
    std::string name(reinterpret_cast<const char *>(entry),
                     strnlen(reinterpret_cast<const char *>(entry), 16));
    Section section;
    section.offset = ReadLe<std::uint64_t>(entry + 16);
    section.size = ReadLe<std::uint64_t>(entry + 24);
    section.rawSize = ReadLe<std::uint64_t>(entry + 32);
    section.codec = ReadLe<std::uint32_t>(entry + 40);
    section.frameSize = ReadLe<std::uint32_t>(entry + 44);
    if (section.offset + section.size > fileSize ||
        (section.codec != 0 && section.codec != 2)) {
      return fail("セクション " + name + " が壊れています");
    }
    sections[name] = section;
  }
  for (const char *name : {"meta", "in_lists", "in_len", "titles", "title_ids",
                           "wiki_ids", "popular"}) {
    if (!sections.count(name)) {
      return fail(std::string("セクション ") + name + " がありません");
    }
  }

  // 展開の仕事を並べる。圧縮したセクションは独立したフレームの列なので、
  // フレームごとに別スレッドで展開できる。
  struct Job {
    const std::uint8_t *src;
    std::size_t srcSize;
    std::uint8_t *dst;
    std::size_t dstSize;
    bool compressed;
  };
  std::vector<Job> jobs;
  const auto addJobs = [&](const Section &s, std::uint8_t *dst) {
    const std::uint8_t *src = data.data() + s.offset;
    if (s.codec == 0) {
      if (s.size != s.rawSize) {
        return false;
      }
      jobs.push_back({src, static_cast<std::size_t>(s.size), dst,
                      static_cast<std::size_t>(s.rawSize), false});
      return true;
    }
    if (s.size < 8) {
      return false;
    }
    const std::uint32_t frames = ReadLe<std::uint32_t>(src);
    const std::uint64_t tableEnd = 8ull + 8ull * frames;
    if (frames == 0 || tableEnd > s.size ||
        (frames > 1 && (s.frameSize == 0 ||
                        static_cast<std::uint64_t>(s.frameSize) * (frames - 1) >= s.rawSize))) {
      return false;
    }
    std::uint64_t pos = tableEnd;
    for (std::uint32_t f = 0; f < frames; ++f) {
      const std::uint64_t compressed = ReadLe<std::uint64_t>(src + 8 + 8ull * f);
      const std::uint64_t dstOffset = frames == 1 ? 0 : static_cast<std::uint64_t>(f) * s.frameSize;
      const std::uint64_t rawSize =
          frames == 1 ? s.rawSize : std::min<std::uint64_t>(s.frameSize, s.rawSize - dstOffset);
      if (pos + compressed > s.size) {
        return false;
      }
      jobs.push_back({src + pos, static_cast<std::size_t>(compressed), dst + dstOffset,
                      static_cast<std::size_t>(rawSize), true});
      pos += compressed;
    }
    return pos == s.size;
  };
  const auto addSection = [&](const Section &s, auto &out) {
    out.resize(static_cast<std::size_t>(s.rawSize) / sizeof(out[0]));
    if (out.size() * sizeof(out[0]) != s.rawSize) {
      return false;
    }
    return addJobs(s, reinterpret_cast<std::uint8_t *>(out.data()));
  };

  std::shared_ptr<WikiLinkGraph> graph(new WikiLinkGraph());
  std::vector<std::uint8_t> metaRaw, titleIdPlanes, popularRaw;
  std::vector<std::uint32_t> inLen;
  if (!addSection(sections["titles"], graph->m_titles) ||
      !addSection(sections["in_lists"], graph->m_inLists) ||
      !addSection(sections["meta"], metaRaw) ||
      !addSection(sections["in_len"], inLen) ||
      !addSection(sections["title_ids"], titleIdPlanes) ||
      !addSection(sections["wiki_ids"], graph->m_wikiIds) ||
      !addSection(sections["popular"], popularRaw)) {
    return fail("セクションの大きさが合いません");
  }
  std::string &titles = graph->m_titles;
  // 大きい仕事から始めると全体が早く終わる
  std::stable_sort(jobs.begin(), jobs.end(), [](const Job &a, const Job &b) {
    return a.dstSize > b.dstSize;
  });

  std::atomic<bool> ok{true};
  ParallelFor(jobs.size(), DefaultThreads(), [&](std::size_t i) {
    const Job &job = jobs[i];
    if (!ok.load(std::memory_order_relaxed) || Cancelled(cancel)) {
      ok = false;
      return;
    }
    if (!job.compressed) {
      std::memcpy(job.dst, job.src, job.srcSize);
      return;
    }
    DECOMPRESSOR_HANDLE handle = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &handle)) {
      ok = false;
      return;
    }
    SIZE_T got = 0;
    if (!Decompress(handle, job.src, job.srcSize, job.dst, job.dstSize, &got) ||
        got != job.dstSize) {
      ok = false;
    }
    CloseDecompressor(handle);
  });
  data.clear();
  data.shrink_to_fit();
  if (!ok) {
    return fail(Cancelled(cancel) ? "中断しました" : "展開に失敗しました");
  }

  const std::string meta(metaRaw.begin(), metaRaw.end());
  graph->m_source = JsonString(meta, "source");
  graph->m_linkCount = JsonNumber(meta, "links");
  const std::size_t n = graph->m_wikiIds.size();
  if (n == 0 || inLen.size() != n || JsonNumber(meta, "articles") != n) {
    return fail("記事数が合いません");
  }

  // 一覧の開始位置
  graph->m_rowStart.resize(n + 1);
  std::uint64_t offset = 0;
  for (std::size_t i = 0; i < n; ++i) {
    graph->m_rowStart[i] = static_cast<std::uint32_t>(offset);
    offset += inLen[i];
  }
  if (offset != graph->m_inLists.size() || offset > 0xFFFFFFFFull) {
    return fail("入リンク一覧の長さが合いません");
  }
  graph->m_rowStart[n] = static_cast<std::uint32_t>(offset);

  // 見出し → 記事番号（3面に分けて格納されている）
  const std::size_t titleCount = titleIdPlanes.size() / 3;
  graph->m_titleIds.resize(titleCount);
  for (std::size_t i = 0; i < titleCount; ++i) {
    graph->m_titleIds[i] = titleIdPlanes[i] |
                           (static_cast<std::uint32_t>(titleIdPlanes[titleCount + i]) << 8) |
                           (static_cast<std::uint32_t>(titleIdPlanes[2 * titleCount + i]) << 16);
    if (graph->m_titleIds[i] >= n) {
      return fail("タイトルの記事番号が範囲外です");
    }
  }

  // ブロックの開始位置と、各記事の本来のタイトル
  graph->m_ownTitle.assign(n, kInvalid);
  graph->m_blockOffset.reserve(titleCount / kTitleBlock + 1);
  std::size_t pos = 0;
  std::size_t entry = 0;
  while (pos < titles.size()) {
    if (entry >= titleCount) {
      return fail("タイトル数が合いません");
    }
    const auto head = static_cast<std::uint8_t>(titles[pos]);
    if (entry % kTitleBlock == 0) {
      if ((head & 0x7F) != 0) {
        return fail("タイトル辞書が壊れています");
      }
      graph->m_blockOffset.push_back(static_cast<std::uint32_t>(pos));
    }
    if ((head & 0x80) == 0) {
      graph->m_ownTitle[graph->m_titleIds[entry]] = static_cast<std::uint32_t>(entry);
    }
    const auto end = titles.find('\0', pos + 1);
    if (end == std::string::npos) {
      return fail("タイトル辞書が途中で切れています");
    }
    pos = end + 1;
    ++entry;
  }
  if (entry != titleCount) {
    return fail("タイトル数が合いません");
  }

  // page_id → 記事番号
  graph->m_wikiSorted.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    graph->m_wikiSorted[i] = {graph->m_wikiIds[i], static_cast<std::uint32_t>(i)};
  }
  std::sort(graph->m_wikiSorted.begin(), graph->m_wikiSorted.end());

  // 人気記事
  if (popularRaw.size() >= 4) {
    const std::uint32_t count = ReadLe<std::uint32_t>(popularRaw.data());
    if (4ull + 8ull * count != popularRaw.size()) {
      return fail("popular が壊れています");
    }
    graph->m_popular.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::uint32_t id = ReadLe<std::uint32_t>(popularRaw.data() + 4 + 8ull * i);
      const std::uint32_t indeg = ReadLe<std::uint32_t>(popularRaw.data() + 8 + 8ull * i);
      if (id < n) {
        graph->m_popular.emplace_back(id, indeg);
      }
    }
  }
  return graph;
}

namespace {
std::mutex g_sharedMutex;
std::shared_ptr<const WikiLinkGraph> g_shared;
std::string g_sharedPath;
} // namespace

std::shared_ptr<const WikiLinkGraph>
WikiLinkGraph::Shared(const std::string &path, const std::atomic<bool> *cancel) {
  std::lock_guard<std::mutex> lock(g_sharedMutex);
  if (g_shared && g_sharedPath == path) {
    return g_shared;
  }
  std::string error;
  auto graph = Load(path, &error, cancel);
  if (graph) {
    g_shared = graph;
    g_sharedPath = path;
  }
  return graph;
}

void WikiLinkGraph::ReleaseShared() {
  std::lock_guard<std::mutex> lock(g_sharedMutex);
  g_shared.reset();
  g_sharedPath.clear();
}

std::string WikiLinkGraph::TitleEntry(std::uint32_t entry) const {
  const std::uint32_t block = entry / kTitleBlock;
  std::size_t pos = m_blockOffset[block];
  std::string current;
  for (std::uint32_t i = block * kTitleBlock;; ++i) {
    const auto head = static_cast<std::uint8_t>(m_titles[pos]);
    const auto end = m_titles.find('\0', pos + 1);
    current.resize(head & 0x7F);
    current.append(m_titles, pos + 1, end - pos - 1);
    if (i == entry) {
      return current;
    }
    pos = end + 1;
  }
}

std::uint32_t WikiLinkGraph::FindTitle(std::string_view key) const {
  if (key.empty() || m_blockOffset.empty()) {
    return kInvalid;
  }
  // key 以下の先頭タイトルを持つ最後のブロック
  const auto firstTitle = [&](std::uint32_t block) {
    const std::size_t start = m_blockOffset[block] + 1;
    return std::string_view(m_titles.data() + start,
                            std::strlen(m_titles.data() + start));
  };
  std::uint32_t lo = 0;
  std::uint32_t hi = static_cast<std::uint32_t>(m_blockOffset.size());
  while (lo < hi) {
    const std::uint32_t mid = (lo + hi) / 2;
    if (firstTitle(mid) <= key) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo == 0) {
    return kInvalid;
  }
  const std::uint32_t block = lo - 1;
  std::size_t pos = m_blockOffset[block];
  std::string current;
  const std::uint32_t last = std::min<std::uint32_t>(
      (block + 1) * kTitleBlock, static_cast<std::uint32_t>(m_titleIds.size()));
  for (std::uint32_t i = block * kTitleBlock; i < last; ++i) {
    const auto head = static_cast<std::uint8_t>(m_titles[pos]);
    const auto end = m_titles.find('\0', pos + 1);
    current.resize(head & 0x7F);
    current.append(m_titles, pos + 1, end - pos - 1);
    const int order = std::string_view(current).compare(key);
    if (order == 0) {
      return m_titleIds[i];
    }
    if (order > 0) {
      break;
    }
    pos = end + 1;
  }
  return kInvalid;
}

std::string WikiLinkGraph::Title(std::uint32_t article) const {
  if (article >= m_ownTitle.size() || m_ownTitle[article] == kInvalid) {
    return {};
  }
  return TitleEntry(m_ownTitle[article]);
}

std::uint32_t WikiLinkGraph::FromWikiPageId(std::uint32_t wikiPageId) const {
  const auto it = std::lower_bound(
      m_wikiSorted.begin(), m_wikiSorted.end(),
      std::pair<std::uint32_t, std::uint32_t>{wikiPageId, 0});
  if (it == m_wikiSorted.end() || it->first != wikiPageId) {
    return kInvalid;
  }
  return it->second;
}

std::uint32_t WikiLinkGraph::IncomingCount(std::uint32_t article) const {
  if (article >= ArticleCount()) {
    return 0;
  }
  std::uint32_t count = 0;
  for (std::uint32_t p = m_rowStart[article]; p < m_rowStart[article + 1]; ++p) {
    count += (m_inLists[p] & 0x80) == 0;
  }
  return count;
}

std::shared_ptr<WikiLinkGraph::Distances>
WikiLinkGraph::ComputeDistances(std::uint32_t target,
                                const std::vector<std::uint8_t> &blocked,
                                unsigned threads,
                                const std::atomic<bool> *cancel) const {
  auto result = std::make_shared<Distances>();
  const std::uint32_t n = ArticleCount();
  if (target >= n) {
    return result;
  }
  if (threads == 0) {
    threads = DefaultThreads();
  }
  const bool useBlocked = blocked.size() == n;
  result->target = target;
  result->hops.assign(n, kUnreachable);
  result->next.assign(n, kInvalid);
  std::uint8_t *hops = result->hops.data();
  std::uint32_t *next = result->next.data();
  hops[target] = 0;
  std::vector<std::uint32_t> frontier{target};
  std::size_t reached = 1;

  // 小さいフロンティアは1スレッドで、大きいものは分けて展開する
  constexpr std::size_t kParallelFrontier = 2048;
  constexpr std::size_t kChunk = 512;
  std::vector<std::vector<std::uint32_t>> locals(threads);

  for (std::uint32_t level = 1; !frontier.empty() && level < kUnreachable; ++level) {
    if (Cancelled(cancel)) {
      result->cancelled = true;
      break;
    }
    const auto level8 = static_cast<std::uint8_t>(level);
    std::vector<std::uint32_t> nextFrontier;
    if (frontier.size() < kParallelFrontier || threads <= 1) {
      for (const std::uint32_t v : frontier) {
        ForEachIncoming(v, [&](std::uint32_t u) {
          if (hops[u] == kUnreachable && !(useBlocked && blocked[u])) {
            hops[u] = level8;
            next[u] = v;
            nextFrontier.push_back(u);
          }
        });
      }
    } else {
      const std::size_t chunks = (frontier.size() + kChunk - 1) / kChunk;
      std::atomic<std::size_t> nextChunk{0};
      const unsigned workers = static_cast<unsigned>(
          std::min<std::size_t>(threads, chunks));
      std::vector<std::thread> pool;
      pool.reserve(workers);
      for (unsigned t = 0; t < workers; ++t) {
        pool.emplace_back([&, t] {
          auto &local = locals[t];
          local.clear();
          for (std::size_t c = nextChunk.fetch_add(1); c < chunks;
               c = nextChunk.fetch_add(1)) {
            if (Cancelled(cancel)) {
              return;
            }
            const std::size_t end = std::min(frontier.size(), (c + 1) * kChunk);
            for (std::size_t k = c * kChunk; k < end; ++k) {
              const std::uint32_t v = frontier[k];
              ForEachIncoming(v, [&](std::uint32_t u) {
                if (std::atomic_ref<std::uint8_t>(hops[u]).load(
                        std::memory_order_relaxed) != kUnreachable ||
                    (useBlocked && blocked[u])) {
                  return;
                }
                std::uint8_t expected = kUnreachable;
                if (std::atomic_ref<std::uint8_t>(hops[u]).compare_exchange_strong(
                        expected, level8, std::memory_order_relaxed)) {
                  next[u] = v;
                  local.push_back(u);
                }
              });
            }
          }
        });
      }
      for (auto &worker : pool) {
        worker.join();
      }
      std::size_t total = 0;
      for (unsigned t = 0; t < workers; ++t) {
        total += locals[t].size();
      }
      nextFrontier.reserve(total);
      for (unsigned t = 0; t < workers; ++t) {
        nextFrontier.insert(nextFrontier.end(), locals[t].begin(), locals[t].end());
      }
      // 記事番号順に並べると、次の段で一覧を読む位置が順に進んで速い
      std::sort(nextFrontier.begin(), nextFrontier.end());
    }
    reached += nextFrontier.size();
    frontier.swap(nextFrontier);
  }
  result->reached = reached;
  return result;
}

std::size_t WikiLinkGraph::MemoryBytes() const {
  return m_inLists.capacity() + m_rowStart.capacity() * 4 + m_wikiIds.capacity() * 4 +
         m_wikiSorted.capacity() * 8 + m_popular.capacity() * 8 + m_titles.capacity() +
         m_blockOffset.capacity() * 4 + m_titleIds.capacity() * 4 + m_ownTitle.capacity() * 4;
}

} // namespace game::systems
