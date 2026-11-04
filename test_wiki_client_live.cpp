/**
 * @file test_wiki_client_live.cpp
 * @brief 実際のWikipediaに接続してWikiClientの動作を確認する手動ツールです。
 *
 * ネットワークに依存するためctestには含めません。
 *   cmake --build <build> --config Debug --target test_wiki_client_live
 */

#include "src/game/systems/WikiClient.h"
#include "src/graphics/html/CourseHtml.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using game::systems::WikiClient;
using game::systems::WikiImageInfo;

namespace {

int g_failures = 0;

void Check(bool condition, const char *label) {
  std::printf("  [%s] %s\n", condition ? "OK" : "NG", label);
  if (!condition) {
    ++g_failures;
  }
}

void CheckArticle(const std::string &title, bool expectAllowedImage) {
  WikiClient client;
  std::printf("== %s\n", title.c_str());
  const std::string html = client.FetchPageHtml(title);
  const auto urls = graphics::html::ExtractImageUrls(html);

  std::vector<WikiImageInfo> candidates;
  for (const auto &url : urls) {
    const std::string name = graphics::html::ImageFileName(url);
    if (!name.empty()) {
      WikiImageInfo info;
      info.fileTitle = "ファイル:" + name;
      info.thumbUrl = url;
      candidates.push_back(std::move(info));
    }
  }
  const std::size_t candidateCount = candidates.size();
  client.FilterImagesByAllowedLicense(candidates);

  std::unordered_set<std::string> allowed;
  for (const auto &info : candidates) {
    allowed.insert(info.thumbUrl);
  }
  const auto prepared = graphics::html::PrepareArticle(title, html, &allowed);
  std::printf("  html=%zu urls=%zu named=%zu licensed=%zu shown=%zu\n", html.size(),
              urls.size(), candidateCount, candidates.size(),
              prepared.imageUrls.size());
  Check(!prepared.html.empty(), "article prepared");
  Check(prepared.html.find("data-course-target") != std::string::npos,
        "article links kept");
  if (expectAllowedImage) {
    Check(!prepared.imageUrls.empty(), "licensed image kept");
    if (!prepared.imageUrls.empty()) {
      Check(!client.DownloadBinary(prepared.imageUrls[0], 8u * 1024 * 1024).empty(),
            "licensed image downloaded");
    }
  }
}

} // namespace

int main() {
  SetConsoleOutputCP(CP_UTF8);

  CheckArticle("ゴルフ", true);
  CheckArticle("東京タワー", true);
  CheckArticle("織田信長", true);
  CheckArticle("AC/DC", false);
  CheckArticle("USA", false);  // 転送ページ

  {
    std::printf("== 非フリー画像\n");
    WikiClient client;
    std::vector<WikiImageInfo> images(1);
    images[0].fileTitle = "ファイル:存在しない画像のテスト_WikiGOLF.jpg";
    client.FilterImagesByAllowedLicense(images);
    Check(images.empty(), "license unknown is rejected");
  }
  {
    std::printf("== 存在しない記事とエラー応答\n");
    WikiClient client;
    Check(client.FetchPageHtml("存在しない記事のテスト_WikiGOLF").empty(),
          "missing article returns empty");
    Check(client.DownloadBinary(
              "https://thumb.wikimedia.org/wikipedia/commons/thumb/6/6e/"
              "Golfer_swing.jpg/256px-Golfer_swing.jpg")
              .empty(),
          "HTTP error body is not treated as image data");
  }
  {
    std::printf("== サムネイル\n");
    WikiClient client;
    Check(!client.FetchPageThumbnail("ゴルフ").empty(), "CC0 thumbnail returned");
  }
  {
    std::printf("== 並行リクエストの直列化\n");
    constexpr int kThreads = 6;
    const auto startedAt = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back([] {
        WikiClient client;
        client.FetchPageCategories("ゴルフ");
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - startedAt)
                             .count();
    std::printf("  %d requests in %lld ms\n", kThreads,
                static_cast<long long>(elapsed));
    Check(elapsed >= (kThreads - 1) * 220, "at most 5 requests per second");
  }

  std::printf("failures=%d\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
