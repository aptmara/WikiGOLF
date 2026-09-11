/**
 * @file WikiPageDataFetcher.cpp
 * @brief Wikipedia記事データの取得処理を実装します。
*/

#include "WikiPageDataFetcher.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include <chrono>
#include <unordered_map>
#include <utility>
#ifdef WIKIGOLF_HTML_COURSES
#include "../../graphics/html/CourseHtml.h"
#endif

namespace game::scenes {
namespace {

constexpr int kMaxWikiImagesPerPage = 4;

long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}

} // namespace

void WikiPageDataFetcher::SetPreloadedData(
    std::vector<game::WikiLink> links, std::string extract,
    bool skipSupplementalFetch) {
    m_preloadedLinks = std::move(links);
    m_preloadedExtract = std::move(extract);
    m_hasPreloadedData = true;
    m_skipSupplementalFetch = skipSupplementalFetch;
}

PageDataAsyncResult WikiPageDataFetcher::Fetch(const std::string& pageName) {
    const auto loadStartedAt = std::chrono::steady_clock::now();
    PageDataAsyncResult result;
    result.pageName = pageName;
    game::systems::WikiClient wikiClient;

    if (m_hasPreloadedData) {
        LOG_INFO("WikiPageLoader", "Using preloaded data for async: {}", pageName);
        result.allLinks = std::move(m_preloadedLinks);
        result.articleText = std::move(m_preloadedExtract);
        m_hasPreloadedData = false;
        if (m_skipSupplementalFetch) {
            m_skipSupplementalFetch = false;
            result.hasData = true;
            LOG_INFO("WikiPageLoader",
                     "Offline preloaded page complete page='{}' links={} elapsed={}ms",
                     pageName, result.allLinks.size(), ElapsedMs(loadStartedAt));
            return result;
        }
    } else {
        LOG_INFO("WikiPageLoader", "Fetching live data async for: {}", pageName);
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

    const auto categoryStartedAt = std::chrono::steady_clock::now();
    result.pageCategories = wikiClient.FetchPageCategories(pageName);
    LOG_INFO("WikiPageLoader",
             "FetchPageData category fetch page='{}' categories={} elapsed={}ms",
             pageName, result.pageCategories.size(),
             ElapsedMs(categoryStartedAt));

#ifdef WIKIGOLF_HTML_COURSES
    auto prepared = graphics::html::PrepareArticle(pageName, wikiClient.FetchPageHtml(pageName));
    if (!prepared.html.empty()) {
        result.articleHtml = std::move(prepared.html);
        std::size_t imageBytes = 0;
        for (const auto& url : prepared.imageUrls) {
            if (imageBytes >= 48u * 1024 * 1024) break;
            const auto bytes = wikiClient.DownloadBinary(url, 8u * 1024 * 1024);
            if (bytes.size() > 8u * 1024 * 1024) continue;
            graphics::PendingWikiImage image;
            if (!graphics::DecodeWikiImageFromMemory(bytes, image.pixelsBGRA,
                    image.pixelWidth, image.pixelHeight, 2048)) continue;
            if (imageBytes + image.pixelsBGRA.size() > 48u * 1024 * 1024) continue;
            image.sourceUrl = url;
            imageBytes += image.pixelsBGRA.size();
            result.pendingImages.push_back(std::move(image));
        }
        result.hasData = true;
        LOG_INFO("WikiHtml", "Fetched page='{}' htmlBytes={} images={} elapsed={}ms",
            pageName, result.articleHtml.size(), result.pendingImages.size(), ElapsedMs(loadStartedAt));
        return result;
    }
    LOG_WARN("WikiHtml", "Using text fallback: {}", prepared.error);
#endif
    const auto tableTextStartedAt = std::chrono::steady_clock::now();
    const std::string tableText = wikiClient.FetchPageTableText(pageName);
    if (!tableText.empty()) {
        result.articleText += "\n== 表・インフォボックス ==\n";
        result.articleText += tableText;
    }
    LOG_INFO("WikiPageLoader",
             "FetchPageData table text page='{}' bytes={} elapsed={}ms",
             pageName, tableText.size(), ElapsedMs(tableTextStartedAt));

    const auto imagesStartedAt = std::chrono::steady_clock::now();
    result.pendingImages = FetchAndDecodeImages(wikiClient, pageName);
    LOG_INFO("WikiPageLoader",
             "FetchPageData images page='{}' count={} elapsed={}ms",
             pageName, result.pendingImages.size(),
             ElapsedMs(imagesStartedAt));

    result.hasData = true;
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
    const std::string& pageName) const {
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
