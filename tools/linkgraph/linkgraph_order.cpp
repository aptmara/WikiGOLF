/**
 * @file linkgraph_order.cpp
 * @brief リンクグラフの重複除去・ID振り直し（再帰的グラフ二分割）・入リンク一覧の符号化を行います。
 *
 * build_linkgraph.py から呼ばれます。全コアを使います。
 *
 *   linkgraph_order <edges.bin> <out_dir> [iterations] [threads]
 *
 * 入力 edges.bin:
 *   u32 node_count, u64 edge_count, u32 src[edge_count], u32 dst[edge_count]
 *   （ID は 0..node_count-1。重複・自己リンクを含んでいてよい）
 *
 * 出力（out_dir）:
 *   rank.bin      u32[node_count]  旧ID -> 新ID
 *   indeg.bin     u32[node_count]  旧IDごとの被リンク数（重複除去後）
 *   in_len.bin    u32[node_count]  新IDの順に、入リンク一覧のバイト数
 *   in_lists.bin  新IDの順に連結した入リンク一覧
 *                 （新IDを昇順に並べ、「前のIDとの差 - 1」を LEB128 で並べたもの。先頭は -1 からの差）
 *
 * 二分割は Dhulipala ら (KDD 2016) の BP を使います。一覧に並ぶ記事を「データ」、
 * 各入リンク一覧を「問い合わせ」とみなし、同じ一覧に並ぶ記事が近い番号になるよう
 * 再帰的に二分します。同じ入力なら、スレッド数に関係なく同じ結果になります。
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point g_start = Clock::now();

void Log(const char *message) {
  const double seconds =
      std::chrono::duration<double>(Clock::now() - g_start).count();
  std::printf("[%7.1fs] %s\n", seconds, message);
  std::fflush(stdout);
}

unsigned g_threads = 1;

/** @brief [0, count) を全スレッドで分担して fn(begin, end) を呼びます。*/
void ParallelRanges(std::size_t count,
                    const std::function<void(std::size_t, std::size_t)> &fn,
                    std::size_t grain = 4096) {
  if (count == 0) {
    return;
  }
  const unsigned threads = static_cast<unsigned>(
      std::min<std::size_t>(g_threads, (count + grain - 1) / grain));
  if (threads <= 1) {
    fn(0, count);
    return;
  }
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (unsigned t = 0; t < threads; ++t) {
    workers.emplace_back([&] {
      for (;;) {
        const std::size_t begin = next.fetch_add(grain);
        if (begin >= count) {
          break;
        }
        fn(begin, std::min(count, begin + grain));
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
}

bool ReadFile(const std::string &path, std::vector<std::uint8_t> &out) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file) {
    return false;
  }
  _fseeki64(file, 0, SEEK_END);
  const long long size = _ftelli64(file);
  _fseeki64(file, 0, SEEK_SET);
  out.resize(static_cast<std::size_t>(size));
  const std::size_t read = std::fread(out.data(), 1, out.size(), file);
  std::fclose(file);
  return read == out.size();
}

bool WriteFile(const std::string &path, const void *data, std::size_t size) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (!file) {
    return false;
  }
  const std::size_t written = std::fwrite(data, 1, size, file);
  std::fclose(file);
  return written == size;
}

/** @brief 隣接リスト（CSR）。row r の要素は items[offsets[r] .. offsets[r+1]) */
struct Csr {
  std::vector<std::uint64_t> offsets;
  std::vector<std::uint32_t> items;
  std::size_t Rows() const { return offsets.size() - 1; }
  std::uint64_t Begin(std::size_t r) const { return offsets[r]; }
  std::uint64_t End(std::size_t r) const { return offsets[r + 1]; }
};

/** @brief rows[i] -> cols[i] の辺から、行ごとに昇順・重複なしの CSR を作ります。*/
Csr BuildCsr(std::uint32_t n, const std::uint32_t *rows, const std::uint32_t *cols,
             std::uint64_t m) {
  Csr csr;
  std::vector<std::atomic<std::uint32_t>> counts(n);
  ParallelRanges(m, [&](std::size_t b, std::size_t e) {
    for (std::size_t i = b; i < e; ++i) {
      counts[rows[i]].fetch_add(1, std::memory_order_relaxed);
    }
  }, 1 << 20);
  std::vector<std::uint64_t> offsets(static_cast<std::size_t>(n) + 1, 0);
  for (std::uint32_t r = 0; r < n; ++r) {
    offsets[r + 1] = offsets[r] + counts[r].load(std::memory_order_relaxed);
  }
  std::vector<std::atomic<std::uint64_t>> cursor(n);
  for (std::uint32_t r = 0; r < n; ++r) {
    cursor[r].store(offsets[r], std::memory_order_relaxed);
  }
  std::vector<std::uint32_t> items(m);
  ParallelRanges(m, [&](std::size_t b, std::size_t e) {
    for (std::size_t i = b; i < e; ++i) {
      items[cursor[rows[i]].fetch_add(1, std::memory_order_relaxed)] = cols[i];
    }
  }, 1 << 20);
  // 行ごとに並べ替えて重複を消す
  std::vector<std::uint32_t> uniqueCount(n);
  ParallelRanges(n, [&](std::size_t b, std::size_t e) {
    for (std::size_t r = b; r < e; ++r) {
      auto first = items.begin() + static_cast<std::ptrdiff_t>(offsets[r]);
      auto last = items.begin() + static_cast<std::ptrdiff_t>(offsets[r + 1]);
      std::sort(first, last);
      uniqueCount[r] = static_cast<std::uint32_t>(std::unique(first, last) - first);
    }
  }, 256);
  csr.offsets.assign(static_cast<std::size_t>(n) + 1, 0);
  for (std::uint32_t r = 0; r < n; ++r) {
    csr.offsets[r + 1] = csr.offsets[r] + uniqueCount[r];
  }
  csr.items.resize(csr.offsets[n]);
  ParallelRanges(n, [&](std::size_t b, std::size_t e) {
    for (std::size_t r = b; r < e; ++r) {
      std::copy_n(items.begin() + static_cast<std::ptrdiff_t>(offsets[r]),
                  uniqueCount[r],
                  csr.items.begin() + static_cast<std::ptrdiff_t>(csr.offsets[r]));
    }
  }, 4096);
  return csr;
}

// ---------------------------------------------------------------------------
// 再帰的グラフ二分割（BP）
// ---------------------------------------------------------------------------

struct Bisection {
  const Csr &dataToQuery;      // データ（一覧に並ぶ記事） -> 問い合わせ（一覧の持ち主）
  std::uint32_t queryCount;
  int iterations;
  std::vector<std::uint32_t> &perm;   // 並び。各タスクは perm の連続区間を受け持つ
  const std::vector<std::uint32_t> &tieKey;  // 葉の中での並び（小さいほど前）
  std::vector<float> log2Table;       // log2Table[k] = log2(k)

  std::vector<std::uint8_t> side;     // データごとの 0/1（現在のタスク内での側）

  Bisection(const Csr &d2q, std::uint32_t queries, int iters,
            std::vector<std::uint32_t> &order,
            const std::vector<std::uint32_t> &tie)
      : dataToQuery(d2q), queryCount(queries), iterations(iters), perm(order),
        tieKey(tie), side(order.size(), 0) {
    log2Table.resize(order.size() + 2);
    log2Table[0] = 0.0f;
    for (std::size_t k = 1; k < log2Table.size(); ++k) {
      log2Table[k] = static_cast<float>(std::log2(static_cast<double>(k)));
    }
  }

  /** @brief 1問い合わせ分のコスト d*log2(n/(d+1)) の差（d0 側 -> d1 側へ1つ動かしたときの減少量）*/
  float MoveGain(std::int32_t from, std::int32_t to, float logFromN,
                 float logToN) const {
    // before: from*log2(nF/(from+1)) + to*log2(nT/(to+1))
    // after : (from-1)*log2(nF/from) + (to+1)*log2(nT/(to+2))
    const float before =
        from * (logFromN - log2Table[from + 1]) + to * (logToN - log2Table[to + 1]);
    const float after = (from - 1) * (logFromN - log2Table[from]) +
                        (to + 1) * (logToN - log2Table[to + 2]);
    return before - after;
  }

  void SortLeaf(std::size_t lo, std::size_t hi) {
    std::sort(perm.begin() + static_cast<std::ptrdiff_t>(lo),
              perm.begin() + static_cast<std::ptrdiff_t>(hi),
              [&](std::uint32_t a, std::uint32_t b) {
                return tieKey[a] != tieKey[b] ? tieKey[a] < tieKey[b] : a < b;
              });
  }

  /**
   * @brief perm[lo,hi) を二分します。
   * @param parallel true なら全スレッドで1タスクを処理（上位の大きなタスク用）
   * @param deg0,deg1 問い合わせごとの各側の個数（呼び出し側が 0 で渡し、0 に戻して返す）
  */
  template <typename Counter>
  void Split(std::size_t lo, std::size_t hi, bool parallel, Counter *deg0,
             Counter *deg1, std::vector<std::uint32_t> *touched) {
    const std::size_t size = hi - lo;
    const std::size_t mid = lo + size / 2;
    const float logN0 = log2Table[mid - lo];
    const float logN1 = log2Table[hi - mid];
    auto range = [&](std::size_t count,
                     const std::function<void(std::size_t, std::size_t)> &fn) {
      if (parallel) {
        ParallelRanges(count, fn, 2048);
      } else {
        fn(0, count);
      }
    };
    for (std::size_t i = lo; i < hi; ++i) {
      side[perm[i]] = i < mid ? 0 : 1;
    }
    std::vector<float> gain(size);
    std::vector<std::uint32_t> left(mid - lo), right(hi - mid);
    for (int it = 0; it < iterations; ++it) {
      // 1. 問い合わせごとに、各側にいくつあるか数える
      if (touched) {
        touched->clear();
      }
      range(size, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) {
          const std::uint32_t v = perm[lo + i];
          Counter *deg = side[v] ? deg1 : deg0;
          for (std::uint64_t k = dataToQuery.Begin(v); k < dataToQuery.End(v); ++k) {
            const std::uint32_t q = dataToQuery.items[k];
            if constexpr (std::is_same_v<Counter, std::atomic<std::int32_t>>) {
              deg[q].fetch_add(1, std::memory_order_relaxed);
            } else {
              if (deg0[q] == 0 && deg1[q] == 0) {
                touched->push_back(q);
              }
              ++deg[q];
            }
          }
        }
      });
      // 2. 反対側へ動かしたときの得を出す
      range(size, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) {
          const std::uint32_t v = perm[lo + i];
          float g = 0.0f;
          const bool s = side[v] != 0;
          for (std::uint64_t k = dataToQuery.Begin(v); k < dataToQuery.End(v); ++k) {
            const std::uint32_t q = dataToQuery.items[k];
            const std::int32_t d0 = static_cast<std::int32_t>(deg0[q]);
            const std::int32_t d1 = static_cast<std::int32_t>(deg1[q]);
            g += s ? MoveGain(d1, d0, logN1, logN0) : MoveGain(d0, d1, logN0, logN1);
          }
          gain[i] = g;
        }
      });
      // 3. 各側を得の大きい順に並べ、組にして合計が正なら入れ替える
      std::size_t nl = 0, nr = 0;
      for (std::size_t i = 0; i < size; ++i) {
        const std::uint32_t v = perm[lo + i];
        if (side[v]) {
          right[nr++] = static_cast<std::uint32_t>(i);
        } else {
          left[nl++] = static_cast<std::uint32_t>(i);
        }
      }
      auto byGain = [&](std::uint32_t a, std::uint32_t b) {
        return gain[a] != gain[b] ? gain[a] > gain[b] : perm[lo + a] < perm[lo + b];
      };
      std::sort(left.begin(), left.begin() + static_cast<std::ptrdiff_t>(nl), byGain);
      std::sort(right.begin(), right.begin() + static_cast<std::ptrdiff_t>(nr), byGain);
      std::size_t swaps = 0;
      for (std::size_t k = 0; k < std::min(nl, nr); ++k) {
        if (gain[left[k]] + gain[right[k]] <= 0.0f) {
          break;
        }
        std::swap(perm[lo + left[k]], perm[lo + right[k]]);
        ++swaps;
      }
      // 4. 位置で側を決め直し、数えた個数を 0 に戻す
      for (std::size_t i = lo; i < hi; ++i) {
        side[perm[i]] = i < mid ? 0 : 1;
      }
      if constexpr (std::is_same_v<Counter, std::atomic<std::int32_t>>) {
        ParallelRanges(queryCount, [&](std::size_t b, std::size_t e) {
          for (std::size_t q = b; q < e; ++q) {
            deg0[q].store(0, std::memory_order_relaxed);
            deg1[q].store(0, std::memory_order_relaxed);
          }
        }, 1 << 16);
      } else {
        for (std::uint32_t q : *touched) {
          deg0[q] = 0;
          deg1[q] = 0;
        }
      }
      if (swaps == 0) {
        break;
      }
    }
  }

  void Run(std::size_t leafSize) {
    struct Task {
      std::size_t lo, hi;
    };
    // 上位: タスクが少ないうちは1タスクを全スレッドで処理する
    std::vector<Task> level{{0, perm.size()}};
    std::vector<std::atomic<std::int32_t>> deg0(queryCount), deg1(queryCount);
    for (auto &d : deg0) d.store(0, std::memory_order_relaxed);
    for (auto &d : deg1) d.store(0, std::memory_order_relaxed);
    int depth = 0;
    while (!level.empty() && level.size() < g_threads) {
      std::vector<Task> next;
      for (const Task &task : level) {
        if (task.hi - task.lo <= leafSize) {
          SortLeaf(task.lo, task.hi);
          continue;
        }
        Split(task.lo, task.hi, true, deg0.data(), deg1.data(), nullptr);
        const std::size_t mid = task.lo + (task.hi - task.lo) / 2;
        next.push_back({task.lo, mid});
        next.push_back({mid, task.hi});
      }
      level.swap(next);
      ++depth;
      char msg[96];
      std::snprintf(msg, sizeof msg, "bisection depth %d done (parallel within task)", depth);
      Log(msg);
    }
    // 下位: 残りのタスクを共有の作業キューに入れ、空いたスレッドが拾う。
    // 大きい子タスクはキューに戻して他のスレッドにも回し、小さいものは手元で葉まで進める
    std::mutex queueMutex;
    std::condition_variable queueReady;
    std::deque<Task> queue(level.begin(), level.end());
    std::size_t active = 0;
    const std::size_t shareAbove = perm.size() / (g_threads * 64) + leafSize;
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < g_threads; ++t) {
      workers.emplace_back([&] {
        std::vector<std::int32_t> local0(queryCount, 0), local1(queryCount, 0);
        std::vector<std::uint32_t> touched;
        std::vector<Task> stack;
        for (;;) {
          Task task{};
          {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueReady.wait(lock, [&] { return !queue.empty() || active == 0; });
            if (queue.empty()) {
              break;   // キューが空で、子タスクを作りうる作業中のスレッドもいない
            }
            task = queue.front();
            queue.pop_front();
            ++active;
          }
          stack.push_back(task);
          while (!stack.empty()) {
            const Task current = stack.back();
            stack.pop_back();
            if (current.hi - current.lo <= leafSize) {
              SortLeaf(current.lo, current.hi);
              continue;
            }
            Split(current.lo, current.hi, false, local0.data(), local1.data(), &touched);
            const std::size_t mid = current.lo + (current.hi - current.lo) / 2;
            if (current.hi - current.lo > shareAbove) {
              std::lock_guard<std::mutex> lock(queueMutex);
              queue.push_back({mid, current.hi});
              queueReady.notify_one();
            } else {
              stack.push_back({mid, current.hi});
            }
            stack.push_back({current.lo, mid});
          }
          {
            std::lock_guard<std::mutex> lock(queueMutex);
            --active;
          }
          queueReady.notify_all();
        }
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
  }
};

/** @brief 昇順の値列を LEB128 の差分（先頭は -1 からの差、以降は差-1）で書きます。*/
std::size_t EncodeList(const std::uint32_t *values, std::size_t count,
                       std::uint8_t *out) {
  std::size_t n = 0;
  std::int64_t prev = -1;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t gap = static_cast<std::uint32_t>(values[i] - prev - 1);
    prev = values[i];
    while (gap >= 0x80) {
      out[n++] = static_cast<std::uint8_t>(gap | 0x80);
      gap >>= 7;
    }
    out[n++] = static_cast<std::uint8_t>(gap);
  }
  return n;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: linkgraph_order <edges.bin> <out_dir> [iterations] [threads]\n");
    return 2;
  }
  const std::string outDir = argv[2];
  const int iterations = argc > 3 ? std::atoi(argv[3]) : 20;
  g_threads = argc > 4 ? static_cast<unsigned>(std::atoi(argv[4]))
                       : std::max(1u, std::thread::hardware_concurrency());
  {
    char msg[64];
    std::snprintf(msg, sizeof msg, "threads=%u iterations=%d", g_threads, iterations);
    Log(msg);
  }

  std::vector<std::uint8_t> raw;
  if (!ReadFile(argv[1], raw) || raw.size() < 12) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  std::uint32_t n = 0;
  std::uint64_t m = 0;
  std::memcpy(&n, raw.data(), 4);
  std::memcpy(&m, raw.data() + 4, 8);
  if (raw.size() != 12 + m * 8) {
    std::fprintf(stderr, "edges.bin size mismatch\n");
    return 1;
  }
  const auto *src = reinterpret_cast<const std::uint32_t *>(raw.data() + 12);
  const auto *dst = src + m;
  for (std::uint64_t i = 0; i < m; ++i) {
    if (src[i] >= n || dst[i] >= n) {
      std::fprintf(stderr, "edge %llu out of range\n", static_cast<unsigned long long>(i));
      return 1;
    }
  }
  Log("edges loaded");

  // データ -> 問い合わせ（記事 u -> u がリンクしている記事 q）。自己リンクは捨てる
  std::vector<std::uint32_t> s2, d2;
  s2.reserve(m);
  d2.reserve(m);
  for (std::uint64_t i = 0; i < m; ++i) {
    if (src[i] != dst[i]) {
      s2.push_back(src[i]);
      d2.push_back(dst[i]);
    }
  }
  raw.clear();
  raw.shrink_to_fit();
  Csr out = BuildCsr(n, s2.data(), d2.data(), s2.size());
  s2.clear();
  s2.shrink_to_fit();
  d2.clear();
  d2.shrink_to_fit();
  {
    char msg[96];
    std::snprintf(msg, sizeof msg, "out-CSR built: %llu unique links",
                  static_cast<unsigned long long>(out.items.size()));
    Log(msg);
  }

  std::vector<std::uint32_t> indeg(n, 0);
  for (std::uint32_t q : out.items) {
    ++indeg[q];
  }
  // 初期の並び: 被リンク数の多い順（同数は旧ID順）。葉の中でもこの順を使う
  std::vector<std::uint32_t> perm(n);
  std::iota(perm.begin(), perm.end(), 0u);
  std::sort(perm.begin(), perm.end(), [&](std::uint32_t a, std::uint32_t b) {
    return indeg[a] != indeg[b] ? indeg[a] > indeg[b] : a < b;
  });
  std::vector<std::uint32_t> tieKey(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    tieKey[perm[i]] = i;
  }

  Bisection bp(out, n, iterations, perm, tieKey);
  bp.Run(16);
  Log("bisection done");

  std::vector<std::uint32_t> rank(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    rank[perm[i]] = i;
  }

  // 入リンク一覧（新ID）: 行 rank[q] に rank[u] を並べる
  std::vector<std::uint32_t> rows(out.items.size()), cols(out.items.size());
  ParallelRanges(n, [&](std::size_t b, std::size_t e) {
    for (std::size_t u = b; u < e; ++u) {
      for (std::uint64_t k = out.Begin(u); k < out.End(u); ++k) {
        rows[k] = rank[out.items[k]];
        cols[k] = rank[u];
      }
    }
  }, 4096);
  out = Csr{};
  Csr in = BuildCsr(n, rows.data(), cols.data(), rows.size());
  rows.clear();
  rows.shrink_to_fit();
  cols.clear();
  cols.shrink_to_fit();
  Log("in-CSR built");

  std::vector<std::uint32_t> inLen(n);
  std::vector<std::uint64_t> byteOffset(static_cast<std::size_t>(n) + 1, 0);
  ParallelRanges(n, [&](std::size_t b, std::size_t e) {
    for (std::size_t r = b; r < e; ++r) {
      std::size_t bytes = 0;
      std::int64_t prev = -1;
      for (std::uint64_t k = in.Begin(r); k < in.End(r); ++k) {
        std::uint32_t gap = static_cast<std::uint32_t>(in.items[k] - prev - 1);
        prev = in.items[k];
        bytes += gap < (1u << 7) ? 1 : gap < (1u << 14) ? 2 : gap < (1u << 21) ? 3 : gap < (1u << 28) ? 4 : 5;
      }
      inLen[r] = static_cast<std::uint32_t>(bytes);
    }
  }, 4096);
  for (std::uint32_t r = 0; r < n; ++r) {
    byteOffset[r + 1] = byteOffset[r] + inLen[r];
  }
  std::vector<std::uint8_t> stream(byteOffset[n]);
  ParallelRanges(n, [&](std::size_t b, std::size_t e) {
    for (std::size_t r = b; r < e; ++r) {
      EncodeList(in.items.data() + in.Begin(r), in.End(r) - in.Begin(r),
                 stream.data() + byteOffset[r]);
    }
  }, 4096);
  {
    char msg[96];
    std::snprintf(msg, sizeof msg, "encoded in-lists: %.1f MB",
                  static_cast<double>(stream.size()) / 1e6);
    Log(msg);
  }

  if (!WriteFile(outDir + "/rank.bin", rank.data(), rank.size() * 4) ||
      !WriteFile(outDir + "/indeg.bin", indeg.data(), indeg.size() * 4) ||
      !WriteFile(outDir + "/in_len.bin", inLen.data(), inLen.size() * 4) ||
      !WriteFile(outDir + "/in_lists.bin", stream.data(), stream.size())) {
    std::fprintf(stderr, "write failed\n");
    return 1;
  }
  Log("written");
  return 0;
}
