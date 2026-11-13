/**
 * @file WikiPageDataFetcher.cpp
 * @brief Wikipedia記事データの取得処理を実装します。
*/

#include "WikiPageDataFetcher.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../systems/WikiRequestPolicy.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef WIKIGOLF_HTML_COURSES
#include "../../graphics/html/CourseHtml.h"
#endif

namespace game::scenes {
namespace {

constexpr int kMaxWikiImagesPerPage = 4;
/** 覚えておく記事の合計サイズ。画像の画素が大半を占める */
constexpr std::size_t kRememberedPagesByteBudget = 128u * 1024 * 1024;

long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}

void SetProgress(const PageFetchProgress& progress, float value) {
    if (progress) {
        progress->store(std::clamp(value, 0.0f, 1.0f),
                        std::memory_order_relaxed);
    }
}

std::size_t PageBytes(const PageDataAsyncResult& page) {
    std::size_t bytes = page.articleHtml.size() + page.articleText.size();
    for (const auto& image : page.pendingImages) {
        bytes += image.pixelsBGRA.size();
    }
    return bytes;
}

#ifdef WIKIGOLF_HTML_COURSES
/**
 * @brief 記事内の画像を、決められた同時接続数で取得してデコードします。
 * @details 結果は記事内の出現順に並べます。取得やデコードに失敗した画像は飛ばします。
 */
std::vector<graphics::PendingWikiImage> DownloadArticleImages(
    game::systems::WikiClient& wikiClient,
    const std::vector<std::string>& urls, const PageFetchProgress& progress,
    float progressFrom, float progressTo) {
    constexpr std::size_t kMaxImageFileBytes = 8u * 1024 * 1024;
    constexpr std::size_t kMaxTotalPixelBytes = 48u * 1024 * 1024;

    std::vector<std::optional<graphics::PendingWikiImage>> slots(urls.size());
    std::atomic<std::size_t> nextIndex{0};
    std::atomic<std::size_t> finished{0};
    std::atomic<std::size_t> totalPixelBytes{0};

    const auto worker = [&]() {
        for (;;) {
            const std::size_t index =
                nextIndex.fetch_add(1, std::memory_order_relaxed);
            if (index >= urls.size()) {
                return;
            }
            try {
                if (totalPixelBytes.load() < kMaxTotalPixelBytes) {
                    const auto bytes =
                        wikiClient.DownloadBinary(urls[index], kMaxImageFileBytes);
                    graphics::PendingWikiImage image;
                    if (!bytes.empty() &&
                        graphics::DecodeWikiImageFromMemory(
                            bytes, image.pixelsBGRA, image.pixelWidth,
                            image.pixelHeight, 2048)) {
                        const std::size_t size = image.pixelsBGRA.size();
                        if (totalPixelBytes.fetch_add(size) + size <=
                            kMaxTotalPixelBytes) {
                            image.sourceUrl = urls[index];
                            slots[index] = std::move(image);
                        } else {
                            totalPixelBytes.fetch_sub(size);
                        }
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN("WikiHtml", "Image fetch failed: {}", e.what());
            }
            const std::size_t done = finished.fetch_add(1) + 1;
            SetProgress(progress,
                        progressFrom + (progressTo - progressFrom) *
                                           static_cast<float>(done) /
                                           static_cast<float>(urls.size()));
        }
    };

    // 同時に取りに行く数は画像配信ホストの制限に合わせる。1本はこのスレッドで回す。
    const std::size_t workerCount = std::min<std::size_t>(
        urls.size(),
        static_cast<std::size_t>(
            game::systems::wiki_request::kMaxMediaConcurrentRequests));
    std::vector<std::future<void>> helpers;
    for (std::size_t i = 1; i < workerCount; ++i) {
        helpers.push_back(std::async(std::launch::async, worker));
    }
    if (workerCount > 0) {
        worker();
    }
    for (auto& helper : helpers) {
        helper.wait();
    }

    std::vector<graphics::PendingWikiImage> images;
    for (auto& slot : slots) {
        if (slot) {
            images.push_back(std::move(*slot));
        }
    }
    SetProgress(progress, progressTo);
    return images;
}
#endif

} // namespace

void WikiPageDataFetcher::SetPreloadedData(
    std::vector<game::WikiLink> links, std::string extract,
    bool skipSupplementalFetch) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_preloadedLinks = std::move(links);
    m_preloadedExtract = std::move(extract);
    m_hasPreloadedData = true;
    m_skipSupplementalFetch = skipSupplementalFetch;
}

void WikiPageDataFetcher::SetPreloadedPage(PageDataAsyncResult page) {
    if (!IsUsable(page)) {
        return;
    }
    LOG_INFO("WikiPageLoader", "Preloaded page accepted page='{}'",
             page.pageName);
    Remember(std::make_shared<const PageDataAsyncResult>(std::move(page)));
}

void WikiPageDataFetcher::Remember(
    std::shared_ptr<const PageDataAsyncResult> page) {
    const std::size_t bytes = PageBytes(*page);
    if (bytes > kRememberedPagesByteBudget) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::erase_if(m_rememberedPages, [&](const RememberedPage& remembered) {
        return remembered.page->pageName == page->pageName;
    });
    m_rememberedPages.push_back({std::move(page), bytes});

    std::size_t total = 0;
    for (const auto& remembered : m_rememberedPages) {
        total += remembered.bytes;
    }
    while (total > kRememberedPagesByteBudget && m_rememberedPages.size() > 1) {
        total -= m_rememberedPages.front().bytes;
        m_rememberedPages.erase(m_rememberedPages.begin());
    }
}

PageDataAsyncResult WikiPageDataFetcher::Fetch(const std::string& pageName,
                                               PageFetchProgress progress) {
    const auto loadStartedAt = std::chrono::steady_clock::now();
    std::vector<game::WikiLink> presetLinks;
    std::string presetText;
    std::shared_ptr<const PageDataAsyncResult> remembered;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hasPreloadedData) {
            LOG_INFO("WikiPageLoader", "Using preloaded data for async: {}",
                     pageName);
            presetLinks = std::move(m_preloadedLinks);
            presetText = std::move(m_preloadedExtract);
            m_preloadedLinks.clear();
            m_preloadedExtract.clear();
            m_hasPreloadedData = false;
            if (m_skipSupplementalFetch) {
                m_skipSupplementalFetch = false;
                PageDataAsyncResult result;
                result.pageName = pageName;
                result.allLinks = std::move(presetLinks);
                result.articleText = std::move(presetText);
                result.hasData = true;
                SetProgress(progress, 1.0f);
                LOG_INFO("WikiPageLoader",
                         "Offline preloaded page complete page='{}' links={} "
                         "elapsed={}ms",
                         pageName, result.allLinks.size(),
                         ElapsedMs(loadStartedAt));
                return result;
            }
        } else {
            const auto found = std::find_if(
                m_rememberedPages.begin(), m_rememberedPages.end(),
                [&](const RememberedPage& entry) {
                    return entry.page->pageName == pageName;
                });
            if (found != m_rememberedPages.end()) {
                // 使ったものを最新として並べ直す
                std::rotate(found, found + 1, m_rememberedPages.end());
                remembered = m_rememberedPages.back().page;
            }
        }
    }

    if (remembered) {
        PageDataAsyncResult result = *remembered;
        SetProgress(progress, 1.0f);
        LOG_INFO("WikiPageLoader",
                 "Remembered page reused page='{}' links={} images={} "
                 "elapsed={}ms",
                 pageName, result.allLinks.size(), result.pendingImages.size(),
                 ElapsedMs(loadStartedAt));
        return result;
    }

    PageDataAsyncResult result = FetchLive(
        pageName, progress, std::move(presetLinks), std::move(presetText));
    if (IsUsable(result)) {
        Remember(std::make_shared<const PageDataAsyncResult>(result));
    }
    return result;
}

PageDataAsyncResult WikiPageDataFetcher::FetchLive(
    const std::string& pageName, PageFetchProgress progress,
    std::vector<game::WikiLink> presetLinks, std::string presetText,
    std::shared_future<void> beforeLicenseCheck) {
    const auto loadStartedAt = std::chrono::steady_clock::now();
    PageDataAsyncResult result;
    result.pageName = pageName;
    game::systems::WikiClient wikiClient;
    SetProgress(progress, 0.02f);
    LOG_INFO("WikiPageLoader", "Fetching live data async for: {}", pageName);

#ifdef WIKIGOLF_HTML_COURSES
    // 記事HTMLにはリンク・本文・カテゴリがすべて含まれるので、APIへの追加の
    // 問い合わせは画像のライセンス確認だけで済む。
    const auto htmlStartedAt = std::chrono::steady_clock::now();
    const std::string articleHtml = wikiClient.FetchPageHtml(pageName);
    LOG_INFO("WikiHtml", "FetchPageData html page='{}' bytes={} elapsed={}ms",
             pageName, articleHtml.size(), ElapsedMs(htmlStartedAt));
    SetProgress(progress, 0.35f);

    // ライセンスを確認できた画像だけを本文に残す。確認に失敗したら画像なしで表示する。
    const auto licenseStartedAt = std::chrono::steady_clock::now();
    std::vector<game::systems::WikiImageInfo> imageCandidates;
    for (const auto& url : graphics::html::ExtractImageUrls(articleHtml)) {
        const std::string fileName = graphics::html::ImageFileName(url);
        if (fileName.empty()) continue;
        game::systems::WikiImageInfo candidate;
        candidate.fileTitle = "ファイル:" + fileName;
        candidate.thumbUrl = url;
        imageCandidates.push_back(std::move(candidate));
    }
    // 表示するのは記事の先頭から kMaxImages 枚までなので、出現順に少しずつ確認し、
    // 必要な枚数がそろったら残りは確認しない。まとめて確認すると応答が1秒を超えやすく、
    // 次のリクエストまで5秒空けることになる。
    constexpr std::size_t kLicenseBatchSize = 16;
    const std::size_t candidateCount = imageCandidates.size();
    if (candidateCount > 0 && beforeLicenseCheck.valid()) {
        beforeLicenseCheck.wait();
    }
    std::size_t checkedCount = 0;
    std::unordered_set<std::string> allowedImageUrls;
    while (checkedCount < candidateCount &&
           allowedImageUrls.size() < graphics::html::kMaxImages) {
        const std::size_t batchEnd =
            std::min(candidateCount, checkedCount + kLicenseBatchSize);
        std::vector<game::systems::WikiImageInfo> batch(
            imageCandidates.begin() + static_cast<std::ptrdiff_t>(checkedCount),
            imageCandidates.begin() + static_cast<std::ptrdiff_t>(batchEnd));
        wikiClient.FilterImagesByAllowedLicense(batch);
        for (const auto& candidate : batch) {
            allowedImageUrls.insert(candidate.thumbUrl);
        }
        checkedCount = batchEnd;
    }
    LOG_INFO("WikiHtml",
             "FetchPageData license page='{}' candidates={} checked={} "
             "allowed={} elapsed={}ms",
             pageName, candidateCount, checkedCount, allowedImageUrls.size(),
             ElapsedMs(licenseStartedAt));
    SetProgress(progress, 0.5f);

    auto prepared = graphics::html::PrepareArticle(pageName, articleHtml, &allowedImageUrls);
    if (!prepared.html.empty()) {
        result.articleHtml = std::move(prepared.html);
        result.articleText = std::move(prepared.text);
        result.pageCategories = std::move(prepared.categories);
        result.allLinks.reserve(prepared.linkTargets.size());
        for (auto& target : prepared.linkTargets) {
            result.allLinks.push_back({target, target});
        }

        const auto imagesStartedAt = std::chrono::steady_clock::now();
        result.pendingImages = DownloadArticleImages(
            wikiClient, prepared.imageUrls, progress, 0.5f, 1.0f);
        LOG_INFO("WikiHtml",
                 "FetchPageData images page='{}' requested={} decoded={} "
                 "elapsed={}ms",
                 pageName, prepared.imageUrls.size(),
                 result.pendingImages.size(), ElapsedMs(imagesStartedAt));

        result.hasData = true;
        SetProgress(progress, 1.0f);
        LOG_INFO("WikiHtml",
                 "Fetched page='{}' htmlBytes={} links={} textBytes={} "
                 "categories={} images={} elapsed={}ms",
                 pageName, result.articleHtml.size(), result.allLinks.size(),
                 result.articleText.size(), result.pageCategories.size(),
                 result.pendingImages.size(), ElapsedMs(loadStartedAt));
        return result;
    }
    LOG_WARN("WikiHtml", "Using text fallback: {}", prepared.error);
    // 記事HTMLは取得を試みた後なので、表のテキストのために取り直さない。
    // 取得に失敗した直後の取り直しは、同じだけ待たされることが多い。
    const bool fetchTableHtml = false;
    const std::string& tableSourceHtml = articleHtml;
#else
    const bool fetchTableHtml = true;
    const std::string tableSourceHtml;
#endif

    // 記事HTMLを使えないときは、APIからリンク・本文・カテゴリを個別に取得する。
    if (!presetLinks.empty() || !presetText.empty()) {
        result.allLinks = std::move(presetLinks);
        result.articleText = std::move(presetText);
    } else {
        const auto linksStartedAt = std::chrono::steady_clock::now();
        result.allLinks = wikiClient.FetchPageLinks(pageName, 0);
        LOG_INFO("WikiPageLoader",
                 "FetchPageData links page='{}' count={} elapsed={}ms",
                 pageName, result.allLinks.size(), ElapsedMs(linksStartedAt));
        const auto extractStartedAt = std::chrono::steady_clock::now();
        result.articleText = wikiClient.FetchPageExtract(pageName, 5000);
        LOG_INFO("WikiPageLoader",
                 "FetchPageData extract page='{}' bytes={} elapsed={}ms",
                 pageName, result.articleText.size(), ElapsedMs(extractStartedAt));
    }
    SetProgress(progress, 0.6f);

    const auto categoryStartedAt = std::chrono::steady_clock::now();
    result.pageCategories = wikiClient.FetchPageCategories(pageName);
    LOG_INFO("WikiPageLoader",
             "FetchPageData category fetch page='{}' categories={} elapsed={}ms",
             pageName, result.pageCategories.size(),
             ElapsedMs(categoryStartedAt));
    SetProgress(progress, 0.7f);

    const auto tableTextStartedAt = std::chrono::steady_clock::now();
    const std::string tableText =
        fetchTableHtml
            ? wikiClient.FetchPageTableText(pageName)
            : game::systems::WikiClient::ExtractTableText(tableSourceHtml);
    if (!tableText.empty()) {
        result.articleText += "\n== 表・インフォボックス ==\n";
        result.articleText += tableText;
    }
    LOG_INFO("WikiPageLoader",
             "FetchPageData table text page='{}' bytes={} elapsed={}ms",
             pageName, tableText.size(), ElapsedMs(tableTextStartedAt));
    SetProgress(progress, 0.8f);

    const auto imagesStartedAt = std::chrono::steady_clock::now();
    result.pendingImages = FetchAndDecodeImages(wikiClient, pageName);
    LOG_INFO("WikiPageLoader",
             "FetchPageData images page='{}' count={} elapsed={}ms",
             pageName, result.pendingImages.size(),
             ElapsedMs(imagesStartedAt));

    result.hasData = true;
    SetProgress(progress, 1.0f);
    LOG_INFO("WikiPageLoader",
             "FetchPageData finished page='{}' links={} extractBytes={} "
             "categories={} images={} elapsed={}ms",
             pageName, result.allLinks.size(), result.articleText.size(),
             result.pageCategories.size(), result.pendingImages.size(),
             ElapsedMs(loadStartedAt));
    return result;
}

std::vector<graphics::PendingWikiImage>
WikiPageDataFetcher::FetchAndDecodeImages(
    game::systems::WikiClient& wikiClient,
    const std::string& pageName) {
    std::vector<graphics::PendingWikiImage> images;
    const auto imageInfos =
        wikiClient.FetchPageImages(pageName, kMaxWikiImagesPerPage);
    if (imageInfos.empty()) {
        return images;
    }

    const auto sections = wikiClient.FetchPageSections(pageName);
    std::unordered_map<int, std::string> sectionHeadingById;
    for (const auto& section : sections) {
        sectionHeadingById[section.index] = section.heading;
    }

    for (const auto& info : imageInfos) {
        const std::string bytes = wikiClient.DownloadBinary(info.thumbUrl);
        if (bytes.empty()) {
            continue;
        }

        graphics::PendingWikiImage image;
        if (!graphics::DecodeWikiImageFromMemory(
                bytes, image.pixelsBGRA, image.pixelWidth,
                image.pixelHeight)) {
            LOG_WARN("WikiPageLoader", "Failed to decode image for {}", pageName);
            continue;
        }

        image.caption = core::ToWString(info.caption);
        image.isLead = info.leadImage;
        if (!info.leadImage) {
            const auto heading = sectionHeadingById.find(info.sectionId);
            if (heading == sectionHeadingById.end()) {
                continue;
            }
            image.headingText = core::ToWString(heading->second);
        }
        images.push_back(std::move(image));
    }

    LOG_INFO("WikiPageLoader", "Fetched/decoded {} images for {}",
             images.size(), pageName);
    return images;
}

} // namespace game::scenes
