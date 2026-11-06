/**
 * @file test_wiki_link_graph.cpp
 * @brief リンクグラフ（WikiLinkGraph）と最短経路（WikiShortestPath）のテスト
 *
 * 小さなグラフを圧縮なし（codec 0）の .linkgraph に書き出して読み込み、
 * タイトル検索・転送・手数・除外・人気記事・並列 BFS を確かめます。
*/
#include "game/systems/ContentFilter.h"
#include "game/systems/WikiLinkGraph.h"
#include "game/systems/WikiShortestPath.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using game::systems::ContentFilter;
using game::systems::WikiLinkGraph;
using game::systems::WikiShortestPath;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct TitleEntry {
  std::string title;
  std::uint32_t article;
  bool redirect;
};

struct GraphSpec {
  std::vector<std::vector<std::uint32_t>> incoming; // 記事ごとの入リンク（昇順）
  std::vector<std::uint32_t> wikiIds;
  std::vector<TitleEntry> titles;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> popular;
};

template <class T> void Put(std::string &out, T value) {
  out.append(reinterpret_cast<const char *>(&value), sizeof(T));
}

/** @brief build_linkgraph.py と同じ形式（圧縮なし）で書き出す*/
std::string WriteGraph(const GraphSpec &spec, const std::string &path) {
  const std::size_t n = spec.wikiIds.size();
  std::string inLists;
  std::string inLen;
  std::uint64_t links = 0;
  for (std::size_t v = 0; v < n; ++v) {
    const std::size_t before = inLists.size();
    std::int64_t prev = -1;
    for (const std::uint32_t u : spec.incoming[v]) {
      std::uint32_t gap = static_cast<std::uint32_t>(u - prev - 1);
      prev = u;
      while (gap >= 0x80) {
        inLists.push_back(static_cast<char>(gap | 0x80));
        gap >>= 7;
      }
      inLists.push_back(static_cast<char>(gap));
      ++links;
    }
    Put(inLen, static_cast<std::uint32_t>(inLists.size() - before));
  }
  auto titles = spec.titles;
  std::sort(titles.begin(), titles.end(),
            [](const TitleEntry &a, const TitleEntry &b) { return a.title < b.title; });
  std::string dict;
  std::string planes[3];
  std::string prevTitle;
  for (std::size_t i = 0; i < titles.size(); ++i) {
    std::size_t shared = 0;
    if (i % 64) {
      while (shared < 127 && shared < prevTitle.size() && shared < titles[i].title.size() &&
             prevTitle[shared] == titles[i].title[shared]) {
        ++shared;
      }
    }
    dict.push_back(static_cast<char>(shared | (titles[i].redirect ? 0x80 : 0)));
    dict += titles[i].title.substr(shared);
    dict.push_back('\0');
    prevTitle = titles[i].title;
    for (int k = 0; k < 3; ++k) {
      planes[k].push_back(static_cast<char>((titles[i].article >> (8 * k)) & 0xFF));
    }
  }
  std::string wiki;
  for (const auto id : spec.wikiIds) {
    Put(wiki, id);
  }
  std::string popular;
  Put(popular, static_cast<std::uint32_t>(spec.popular.size()));
  for (const auto &[id, indeg] : spec.popular) {
    Put(popular, id);
    Put(popular, indeg);
  }
  const std::string meta = "{\"format\": \"wikigolf-linkgraph\", \"version\": 2, "
                           "\"source\": \"test\", \"articles\": " +
                           std::to_string(n) + ", \"links\": " + std::to_string(links) + "}";
  const std::vector<std::pair<std::string, std::string>> sections = {
      {"meta", meta},        {"in_lists", inLists},
      {"in_len", inLen},     {"titles", dict},
      {"title_ids", planes[0] + planes[1] + planes[2]},
      {"wiki_ids", wiki},    {"popular", popular}};
  std::string table;
  std::uint64_t offset = 32 + 48 * sections.size();
  for (const auto &[name, data] : sections) {
    char nameField[16] = {};
    std::memcpy(nameField, name.data(), name.size());
    table.append(nameField, 16);
    Put(table, offset);
    Put(table, static_cast<std::uint64_t>(data.size()));
    Put(table, static_cast<std::uint64_t>(data.size()));
    Put(table, static_cast<std::uint32_t>(0));
    Put(table, static_cast<std::uint32_t>(0));
    offset += data.size();
  }
  std::string file = "WGLG";
  Put(file, static_cast<std::uint32_t>(2));
  Put(file, static_cast<std::uint32_t>(sections.size()));
  Put(file, static_cast<std::uint32_t>(0));
  Put(file, offset);
  Put(file, static_cast<std::uint64_t>(0));
  file += table;
  for (const auto &[name, data] : sections) {
    file += data;
  }
  std::ofstream(path, std::ios::binary).write(file.data(), static_cast<std::streamsize>(file.size()));
  return file;
}

/**
 * 記事: 0 "A", 1 "B_(x)", 2 "C", 3 "Apple", 4 "日本", 5 "Isolated"
 * リンク: A→B, B→C, C→日本, Apple→日本, A→Apple
 * 転送: "Bee"→B_(x), "Japan"→日本
*/
GraphSpec SmallGraph() {
  GraphSpec spec;
  spec.wikiIds = {100, 101, 102, 103, 104, 105};
  spec.incoming = {{}, {0}, {1}, {0}, {2, 3}, {}};
  spec.titles = {{"A", 0, false},     {"B_(x)", 1, false},   {"C", 2, false},
                 {"Apple", 3, false}, {"日本", 4, false},    {"Isolated", 5, false},
                 {"Bee", 1, true},    {"Japan", 4, true}};
  spec.popular = {{4, 20000}, {2, 15000}, {3, 6000}};
  return spec;
}

void TestGraphBasics(const std::string &path) {
  std::string error;
  auto graph = WikiLinkGraph::Load(path, &error);
  CHECK(graph != nullptr);
  if (!graph) {
    std::printf("load error: %s\n", error.c_str());
    return;
  }
  CHECK(graph->ArticleCount() == 6);
  CHECK(graph->LinkCount() == 5);
  CHECK(graph->SourceName() == "test");
  CHECK(graph->FindTitle("A") == 0);
  CHECK(graph->FindTitle("B_(x)") == 1);
  CHECK(graph->FindTitle("Bee") == 1);
  CHECK(graph->FindTitle("Japan") == 4);
  CHECK(graph->FindTitle("日本") == 4);
  CHECK(graph->FindTitle("B") == WikiLinkGraph::kInvalid);
  CHECK(graph->FindTitle("apple") == WikiLinkGraph::kInvalid); // 完全一致のみ
  CHECK(graph->FindTitle("") == WikiLinkGraph::kInvalid);
  CHECK(graph->FindTitle("0") == WikiLinkGraph::kInvalid);   // 先頭より前
  CHECK(graph->FindTitle("\xEF\xBD\x9D") == WikiLinkGraph::kInvalid); // 末尾より後
  CHECK(graph->Title(1) == "B_(x)"); // 転送ページ名ではなく本来のタイトル
  CHECK(graph->Title(4) == "日本");
  CHECK(graph->Title(99).empty());
  CHECK(graph->WikiPageId(3) == 103);
  CHECK(graph->FromWikiPageId(105) == 5);
  CHECK(graph->FromWikiPageId(999) == WikiLinkGraph::kInvalid);
  CHECK(graph->IncomingCount(4) == 2);
  CHECK(graph->Popular().size() == 3);

  const auto d = graph->ComputeDistances(4, {});
  CHECK(d->hops[4] == 0);
  CHECK(d->hops[2] == 1);
  CHECK(d->hops[3] == 1);
  CHECK(d->hops[1] == 2);
  CHECK(d->hops[0] == 2);
  CHECK(d->hops[5] == WikiLinkGraph::kUnreachable);
  CHECK(d->reached == 5);
  CHECK(d->next[0] == 3); // A → Apple → 日本
  CHECK(d->next[4] == WikiLinkGraph::kInvalid);

  // Apple を通さなければ A は B → C 経由の3手
  std::vector<std::uint8_t> blocked(6, 0);
  blocked[3] = 1;
  const auto b = graph->ComputeDistances(4, blocked);
  CHECK(b->hops[0] == 3);
  CHECK(b->hops[3] == WikiLinkGraph::kUnreachable);
}

void TestShortestPath(const std::string &path) {
  WikiShortestPath sp;
  CHECK(sp.Initialize(path));
  CHECK(sp.IsAvailable());
  CHECK(sp.ResolvePageId("A") == 100);
  CHECK(sp.ResolvePageId("apple") == 103); // 先頭の ASCII 小文字は大文字でも引く
  CHECK(sp.ResolvePageId("B (x)") == 101); // 空白はアンダースコアとして引く
  CHECK(sp.ResolvePageId("Bee") == 101);   // 転送は転送先
  CHECK(sp.ResolvePageId("Japan") == 104);
  CHECK(sp.ResolvePageId("Nope") == -1);

  auto r = sp.FindShortestPath("A", 104, 4, false);
  CHECK(r.success && r.degrees == 2);
  CHECK((r.path == std::vector<std::string>{"A", "Apple", "日本"}));
  r = sp.FindShortestPath("A", "Japan", 4, false);
  CHECK(r.success && r.degrees == 2);
  r = sp.FindShortestPath("A", 104, 1, false); // 上限より遠い
  CHECK(!r.success);
  r = sp.FindShortestPath("Isolated", 104, 20, false);
  CHECK(!r.success);
  r = sp.FindShortestPath("Nope", 104, 4, false);
  CHECK(!r.success);
  r = sp.FindShortestPath("A", 999, 4, false);
  CHECK(!r.success);
  r = sp.FindShortestPath("Japan", 104, 4, false);
  CHECK(r.success && r.degrees == 0);
  r = sp.FindShortestPath("B (x)", 104, 4, false);
  CHECK(r.success && r.degrees == 2 && r.path.front() == "B (x)");

  std::atomic<size_t> progress{0};
  std::map<std::string, int> notified;
  const auto distances = sp.ComputeDistancesToTarget(
      {"A", "B (x)", "Bee", "Isolated", "Nope", "日本", "", "A"}, 104, 4, &progress, 10,
      [&](const std::string &title, int hops) { notified[title] = hops; });
  CHECK(distances.size() == 4);
  CHECK(distances.at("A") == 2);
  CHECK(distances.at("B (x)") == 2);
  CHECK(distances.at("Bee") == 2);
  CHECK(distances.at("日本") == 0);
  CHECK(notified.size() == 4);
  CHECK(progress.load() == 10 + 8 + 4 * 100);
  const auto near = sp.ComputeDistancesToTarget({"A", "C"}, 104, 1);
  CHECK(near.size() == 1 && near.count("C"));

  // セーフモードで Apple を除外すると、A は3手になる
  std::istringstream filter("2\t103\tApple\n");
  ContentFilter::Instance().LoadFromStream(filter);
  ContentFilter::Instance().SetSafeMode(true);
  r = sp.FindShortestPath("A", 104, 4, false);
  CHECK(r.success && r.degrees == 3);
  ContentFilter::Instance().SetSafeMode(false);
  r = sp.FindShortestPath("A", 104, 4, false);
  CHECK(r.success && r.degrees == 2);

  // 人気記事（被リンク1万以上）だけが候補
  for (std::uint32_t seed = 0; seed < 20; ++seed) {
    const auto [title, id] = sp.FetchPopularPageTitle(10000, seed);
    CHECK((title == "日本" && id == 104) || (title == "C" && id == 102));
  }
  std::istringstream empty("");
  ContentFilter::Instance().LoadFromStream(empty);
  WikiShortestPath::ClearProcessCaches();
}

void TestParallelBfsMatchesSerial(const std::string &path) {
  // 並列化の閾値（フロンティア2048件）を超える大きさのランダムグラフ
  constexpr std::uint32_t n = 30000;
  std::mt19937 rng(12345);
  GraphSpec spec;
  spec.incoming.resize(n);
  for (std::uint32_t v = 0; v < n; ++v) {
    spec.wikiIds.push_back(v * 3 + 7);
    char name[32];
    std::snprintf(name, sizeof(name), "N%06u", v);
    spec.titles.push_back({name, v, false});
    const int degree = static_cast<int>(rng() % 12);
    for (int k = 0; k < degree; ++k) {
      spec.incoming[v].push_back(rng() % n);
    }
    std::sort(spec.incoming[v].begin(), spec.incoming[v].end());
    spec.incoming[v].erase(std::unique(spec.incoming[v].begin(), spec.incoming[v].end()),
                           spec.incoming[v].end());
  }
  WriteGraph(spec, path);
  auto graph = WikiLinkGraph::Load(path);
  CHECK(graph != nullptr);
  if (!graph) {
    return;
  }
  CHECK(graph->FindTitle("N012345") == 12345);
  CHECK(graph->Title(29999) == "N029999");
  for (std::uint32_t target : {0u, 777u, 29999u}) {
    const auto serial = graph->ComputeDistances(target, {}, 1);
    const auto parallel = graph->ComputeDistances(target, {}, 8);
    CHECK(serial->hops == parallel->hops);
    CHECK(serial->reached == parallel->reached);
    CHECK(serial->reached > n / 2);
    // 「次の記事」はゴールへ1手近い
    bool nextOk = true;
    for (std::uint32_t v = 0; v < n; ++v) {
      const auto h = parallel->hops[v];
      if (h != WikiLinkGraph::kUnreachable && h > 0) {
        nextOk &= parallel->hops[parallel->next[v]] + 1 == h;
      }
    }
    CHECK(nextOk);
  }
}

void TestBrokenFiles(const std::string &path, const std::string &content) {
  std::string error;
  CHECK(WikiLinkGraph::Load(path + ".missing", &error) == nullptr);
  CHECK(!error.empty());
  // 途中で切れたファイル
  std::ofstream(path, std::ios::binary)
      .write(content.data(), static_cast<std::streamsize>(content.size() / 2));
  CHECK(WikiLinkGraph::Load(path, &error) == nullptr);
  // 版が違う
  std::string wrongVersion = content;
  wrongVersion[4] = 1;
  std::ofstream(path, std::ios::binary)
      .write(wrongVersion.data(), static_cast<std::streamsize>(wrongVersion.size()));
  CHECK(WikiLinkGraph::Load(path, &error) == nullptr);
  WikiShortestPath sp;
  CHECK(!sp.Initialize(path + ".missing"));
  CHECK(!sp.IsAvailable());
  CHECK(!sp.FindShortestPath("A", 104).success);
  CHECK(sp.ResolvePageId("A") == -1);
  CHECK(sp.FetchPopularPageTitle().second == -1);
}

/**
 * @brief 同梱の実データで、ゲームと同じ順に呼んで結果と時間を確かめる。
 * @details ファイルが無い（Git LFS の実体を取得していない）ときは飛ばす。
*/
void TestRealData(const std::string &path) {
  using Clock = std::chrono::steady_clock;
  const auto ms = [](Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
  };
  std::error_code ec;
  if (std::filesystem::file_size(path, ec) < (1u << 20)) {
    std::printf("SKIP real data: %s が無い（または LFS のポインタのみ）\n", path.c_str());
    return;
  }
  auto started = Clock::now();
  WikiShortestPath sp;
  CHECK(sp.Initialize(path));
  std::printf("real: initialize %.0fms\n", ms(started));
  started = Clock::now();
  WikiShortestPath second; // 2つ目以降は共有済みのグラフを使うので一瞬
  CHECK(second.Initialize(path, false));
  std::printf("real: second initialize %.2fms\n", ms(started));
  CHECK(ms(started) < 50.0);

  CHECK(sp.ResolvePageId("日本") > 0);
  CHECK(sp.ResolvePageId("日本") == sp.ResolvePageId("日本国")); // 転送
  CHECK(sp.ResolvePageId("Japan") == sp.ResolvePageId("日本"));
  CHECK(sp.ResolvePageId("東京都") > 0);
  CHECK(sp.ResolvePageId("存在しない記事のタイトルxyz") == -1);

  // ゴール選び（人気記事）
  int popularOk = 0;
  for (std::uint32_t seed = 0; seed < 50; ++seed) {
    const auto [title, id] = sp.FetchPopularPageTitle(10000, seed);
    popularOk += !title.empty() && id > 0 && sp.ResolvePageId(title) == id;
  }
  std::printf("real: popular picks ok %d/50\n", popularOk);
  CHECK(popularOk >= 45);

  const int japan = sp.ResolvePageId("日本");
  started = Clock::now();
  auto r = sp.FindShortestPath("東京都", japan, 4, false);
  std::printf("real: first path (builds table) %.0fms degrees=%d\n", ms(started), r.degrees);
  CHECK(r.success && r.degrees == 1);
  started = Clock::now();
  int found = 0;
  for (const char *title : {"富士山", "源氏物語", "ポケットモンスター", "東京タワー",
                            "織田信長", "サッカー", "Python", "寿司"}) {
    found += sp.FindShortestPath(title, japan, 20, false).success;
  }
  std::printf("real: 8 cached paths %.2fms found=%d\n", ms(started), found);
  CHECK(found == 8);

  // ページのリンク一括評価（AsyncPathEvaluator と同じ呼び方）
  std::vector<std::string> links;
  for (const char *title : {"富士山", "静岡県", "山梨県", "日本の山", "活火山", "富士箱根伊豆国立公園",
                            "世界遺産", "葛飾北斎", "存在しない記事xyz", "日本"}) {
    links.emplace_back(title);
  }
  const int target = sp.ResolvePageId("第104回日本陸上競技選手権大会");
  CHECK(target > 0);
  started = Clock::now();
  std::atomic<size_t> progress{0};
  const auto distances = sp.ComputeDistancesToTarget(links, target, 6, &progress, 0);
  std::printf("real: batch distances %.0fms resolved=%zu\n", ms(started), distances.size());
  CHECK(distances.size() >= 8);
  CHECK(progress.load() == links.size() + 6 * 100);
  WikiShortestPath::ClearProcessCaches();
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    TestRealData(argv[1]);
    if (g_failures == 0) {
      std::printf("test_wiki_link_graph (real data): all passed\n");
      return 0;
    }
    std::printf("test_wiki_link_graph (real data): %d failure(s)\n", g_failures);
    return 1;
  }
  const auto dir = std::filesystem::temp_directory_path() / "wikigolf_link_graph_test";
  std::filesystem::create_directories(dir);
  const std::string small = (dir / "small.linkgraph").string();
  const std::string content = WriteGraph(SmallGraph(), small);
  TestGraphBasics(small);
  TestShortestPath(small);
  TestParallelBfsMatchesSerial((dir / "random.linkgraph").string());
  TestBrokenFiles((dir / "broken.linkgraph").string(), content);
  std::filesystem::remove_all(dir);
  if (g_failures == 0) {
    std::printf("test_wiki_link_graph: all passed\n");
    return 0;
  }
  std::printf("test_wiki_link_graph: %d failure(s)\n", g_failures);
  return 1;
}
