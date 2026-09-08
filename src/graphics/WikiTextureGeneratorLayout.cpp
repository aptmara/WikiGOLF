/**
 * @file WikiTextureGeneratorLayout.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
*/

#include "WikiTextureGenerator.h"
#include "../core/Logger.h"
#include <algorithm>
#include <cmath>
#include <d2d1_1.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace graphics {

namespace {

/**
 * @brief 見出し1件分の情報（クリーニング後テキスト内の位置）
*/
struct HeadingSpan {
  size_t pos;
  size_t length;
  int level; // 1 = H2相当（==）, 2 = H3相当（===）, 3以上 = H4以降
};

/**
 * @brief 本文中に挿入する画像1件分の配置情報
*/
struct ImagePlacement {
  size_t insertionPos;
  const PendingWikiImage *image;
};

/**
 * @brief 前後の空白（全角スペース含む）を取り除きます。
*/
std::wstring TrimW(const std::wstring &s) {
  size_t a = s.find_first_not_of(L" \t　");
  if (a == std::wstring::npos) return L"";
  size_t b = s.find_last_not_of(L" \t　");
  return s.substr(a, b - a + 1);
}

/**
 * @brief MediaWiki抽出テキスト（exsectionformat=wiki）から見出し記法（==見出し==）を検出し、
 *        記法を取り除いたテキストと見出し位置一覧を返す。
*/
std::wstring ExtractHeadingsAndClean(const std::wstring &text,
                                     std::vector<HeadingSpan> &headings) {
  std::wstring cleaned;
  cleaned.reserve(text.size());
  const size_t len = text.size();
  size_t lineStart = 0;

  while (lineStart <= len) {
    size_t lineEnd = text.find(L'\n', lineStart);
    bool isLast = (lineEnd == std::wstring::npos);
    size_t actualEnd = lineEnd;
    if (isLast) {
      actualEnd = len;
    }
    std::wstring line = text.substr(lineStart, actualEnd - lineStart);

    std::wstring trimmed = TrimW(line);

    std::wstring headingText;
    int level = 0;
    if (trimmed.size() >= 5) {
      size_t eqHead = 0;
      while (eqHead < trimmed.size() && trimmed[eqHead] == L'=') ++eqHead;
      size_t eqTail = 0;
      while (eqTail < trimmed.size() && trimmed[trimmed.size() - 1 - eqTail] == L'=') ++eqTail;
      if (eqHead >= 2 && eqHead <= 6 && eqHead == eqTail &&
          trimmed.size() > eqHead * 2) {
        std::wstring inner = trimmed.substr(eqHead, trimmed.size() - eqHead * 2);
        std::wstring innerTrimmed = TrimW(inner);
        if (!innerTrimmed.empty()) {
          headingText = innerTrimmed;
          level = static_cast<int>(eqHead) - 1;
        }
      }
    }

    if (!headingText.empty()) {
      if (!cleaned.empty()) {
        cleaned += L'\n'; // 見出し前に空行を挟んで余白を確保
      }
      HeadingSpan span;
      span.pos = cleaned.size();
      span.length = headingText.size();
      span.level = level;
      headings.push_back(span);
      cleaned += headingText;
    } else {
      cleaned += line;
    }

    if (isLast) break;
    cleaned += L'\n';
    lineStart = lineEnd + 1;
  }

  return cleaned;
}

/**
 * @brief 見出しレベルに応じたフォントサイズを返す。
*/
float HeadingFontSize(int level) {
  switch (level) {
  case 1: return 92.0f;
  case 2: return 80.0f;
  default: return 72.0f;
  }
}

/**
 * @brief 画像の表示サイズを決める。本家Wikipediaのサムネイルと同様に、
 *        幅を固定し、高さはアスペクト比なりに可変とする（高さは制限しない）。
*/
void ComputeDisplaySize(uint32_t natW, uint32_t natH, float targetWidth,
                       float &outW, float &outH) {
  outW = targetWidth;
  if (natW == 0 || natH == 0) {
    outH = targetWidth;
    return;
  }
  outH = targetWidth * (static_cast<float>(natH) / static_cast<float>(natW));
}

} // namespace

bool WikiTextureGenerator::BeginGenerateTexture(
    WikiTextureGenerationState &state, const std::wstring &title,
    const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height,
    std::vector<PendingWikiImage> pendingImages, const std::string& articleHtml) {

  if (!articleHtml.empty() && BeginHtmlTexture(state, articleHtml, targetPage, pendingImages)) return true;
  state = WikiTextureGenerationState(); // Reset
  state.title = title;
  state.links = links;
  state.pendingImages = std::move(pendingImages);
  state.targetPage = targetPage;
  state.requestedWidth = width;
  state.requestedHeight = height;

  state.marginX = 40.0f;
  const float maxWidth = static_cast<float>(width) - state.marginX * 2;
  const float layoutMaxHeight = 500000.0f;

  // --- タイトルレイアウト ---
  // 本文の開始位置は固定値ではなく、実際に描画されるタイトルの高さから
  // 動的に決定する（タイトルと本文が重なるバグを避けるため）。
  state.titleTopY = 30.0f;
  HRESULT hr = m_dwriteFactory->CreateTextLayout(
      state.title.c_str(), static_cast<UINT32>(state.title.length()),
      m_titleFormat.Get(), maxWidth, 100000.0f, &state.titleLayout);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create title TextLayout");
    return false;
  }

  hr = state.titleLayout->GetMetrics(&state.titleMetrics);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to GetMetrics(title)");
    return false;
  }

  constexpr float kTitleBottomPadding = 20.0f;
  constexpr float kSeparatorGap = 20.0f;
  state.separatorY = state.titleTopY + state.titleMetrics.height + kTitleBottomPadding;
  state.currentY = state.separatorY + kSeparatorGap;

  // --- 見出し抽出 ---
  // exsectionformat=wiki で取得した "== 見出し ==" 記法を検出し、記法を取り除いた
  // テキストで本文レイアウトを組む。見出し部分は後段でフォントサイズを拡大する。
  std::vector<HeadingSpan> headingSpans;
  state.articleText = ExtractHeadingsAndClean(articleText, headingSpans);

  // --- 画像の挿入位置を決定 ---
  // リード画像は本文冒頭、節画像は対応する見出し直後にfloat配置する。
  // （見出しテキストが一致しない画像は配置しない）
  std::vector<ImagePlacement> placements;
  for (const auto &img : state.pendingImages) {
    if (img.isLead) {
      placements.push_back({0, &img});
      continue;
    }
    if (img.headingText.empty()) continue;

    const std::wstring wanted = TrimW(img.headingText);
    for (const auto &h : headingSpans) {
      if (state.articleText.compare(h.pos, h.length, wanted) == 0) {
        placements.push_back({h.pos + h.length, &img});
        break;
      }
    }
  }
  std::stable_sort(placements.begin(), placements.end(),
                   [](const ImagePlacement &a, const ImagePlacement &b) {
                     return a.insertionPos < b.insertionPos;
                   });

  // --- 見出しへのフォントサイズ適用（区画作成時に呼ぶ） ---
  auto applyHeadingSizing = [&](IDWriteTextLayout *layout, size_t segStart,
                                size_t segLen) {
    for (const auto &h : headingSpans) {
      if (h.pos >= segStart && h.pos + h.length <= segStart + segLen) {
        DWRITE_TEXT_RANGE r = {static_cast<UINT32>(h.pos - segStart),
                               static_cast<UINT32>(h.length)};
        layout->SetFontSize(HeadingFontSize(h.level), r);
      }
    }
  };

  // --- 本文の1区画を作成しstate.textSegmentsへ積む。区画の高さを返す ---
  auto makeSegment = [&](size_t segStart, size_t segLen, float yTop,
                        float segWidth) -> float {
    if (segLen == 0) return 0.0f;

    WikiTextSegment seg;
    seg.textStart = segStart;
    seg.textLength = segLen;
    seg.yTop = yTop;

    HRESULT segHr = m_dwriteFactory->CreateTextLayout(
        state.articleText.c_str() + segStart, static_cast<UINT32>(segLen),
        m_bodyFormat.Get(), segWidth, layoutMaxHeight, &seg.layout);
    if (FAILED(segHr)) {
      LOG_ERROR("WikiTexGen", "Failed to create body text segment");
      return 0.0f;
    }

    applyHeadingSizing(seg.layout.Get(), segStart, segLen);

    DWRITE_TEXT_METRICS m{};
    seg.layout->GetMetrics(&m);
    state.textSegments.push_back(std::move(seg));
    return m.height;
  };

  // --- 本文＋画像を順番に流し込む（litehtml等のfloatアルゴリズムを参考にした簡易実装） ---
  constexpr float kImageGap = 20.0f;
  constexpr float kImageBlockGapAfter = 30.0f;
  constexpr float kCaptionPad = 8.0f;
  // 本家Wikipediaのサムネイル既定幅（220~250px相当）より一回り大きく取り、
  // 本文フォントが大きいこのテクスチャでも画像が小さく見えすぎないようにする。
  constexpr float kImageDisplayWidth = 440.0f;
  constexpr float kMinNarrowRatio = 0.3f;

  size_t cursorPos = 0;
  float cursorY = state.currentY;

  for (size_t pi = 0; pi <= placements.size(); ++pi) {
    size_t segEnd = state.articleText.size();
    if (pi < placements.size()) {
      segEnd = placements[pi].insertionPos;
    }
    if (segEnd < cursorPos) segEnd = cursorPos;

    cursorY += makeSegment(cursorPos, segEnd - cursorPos, cursorY, maxWidth);
    cursorPos = segEnd;

    if (pi == placements.size()) break; // 末尾テキストのみ

    const PendingWikiImage &img = *placements[pi].image;
    if (img.pixelsBGRA.empty()) continue;

    ComPtr<ID2D1Bitmap> bitmap = CreateBitmapFromPixels(
        img.pixelsBGRA.data(), img.pixelWidth, img.pixelHeight);
    if (!bitmap) continue;

    float imgW = 0.0f, imgH = 0.0f;
    ComputeDisplaySize(img.pixelWidth, img.pixelHeight, kImageDisplayWidth,
                      imgW, imgH);

    ComPtr<IDWriteTextLayout> captionLayout;
    DWRITE_TEXT_METRICS captionMetrics{};
    if (!img.caption.empty()) {
      m_dwriteFactory->CreateTextLayout(
          img.caption.c_str(), static_cast<UINT32>(img.caption.length()),
          m_captionFormat.Get(), imgW, 100000.0f, &captionLayout);
      if (captionLayout) captionLayout->GetMetrics(&captionMetrics);
    }
    float blockHeight = imgH;
    if (captionLayout) {
      blockHeight += kCaptionPad + captionMetrics.height;
    }
    const float narrowWidth = maxWidth - imgW - kImageGap;

    if (narrowWidth < maxWidth * kMinNarrowRatio) {
      // floatさせるには列が狭すぎる場合は、独立ブロックとして右寄せ配置する
      cursorY += 20.0f;
      WikiPlacedImage placed;
      placed.bitmap = bitmap;
      placed.captionLayout = captionLayout;
      placed.width = imgW;
      placed.height = imgH;
      placed.x = state.marginX + maxWidth - imgW;
      placed.y = cursorY;
      state.placedImages.push_back(std::move(placed));
      cursorY += blockHeight + kImageBlockGapAfter;
      continue;
    }

    // --- 狭幅レイアウトで画像の高さ分にどこまで文字が収まるかを計測 ---
    const size_t remainingLen = state.articleText.size() - cursorPos;
    size_t narrowCharCount = 0;
    if (remainingLen > 0) {
      ComPtr<IDWriteTextLayout> probeLayout;
      HRESULT probeHr = m_dwriteFactory->CreateTextLayout(
          state.articleText.c_str() + cursorPos,
          static_cast<UINT32>(remainingLen), m_bodyFormat.Get(), narrowWidth,
          layoutMaxHeight, &probeLayout);
      if (SUCCEEDED(probeHr)) {
        UINT32 lineCount = 0;
        probeLayout->GetLineMetrics(nullptr, 0, &lineCount);
        if (lineCount > 0) {
          std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
          probeLayout->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount);
          float accHeight = 0.0f;
          size_t accChars = 0;
          for (UINT32 li = 0; li < lineCount; ++li) {
            if (accHeight + lineMetrics[li].height > blockHeight && accChars > 0) {
              break;
            }
            accHeight += lineMetrics[li].height;
            accChars += lineMetrics[li].length;
            if (accHeight >= blockHeight) break;
          }
          narrowCharCount = std::min(accChars, remainingLen);
        }
      }
    }

    float narrowTextHeight = 0.0f;
    if (narrowCharCount > 0) {
      narrowTextHeight =
          makeSegment(cursorPos, narrowCharCount, cursorY, narrowWidth);
    }

    WikiPlacedImage placed;
    placed.bitmap = bitmap;
    placed.captionLayout = captionLayout;
    placed.width = imgW;
    placed.height = imgH;
    placed.x = state.marginX + narrowWidth + kImageGap;
    placed.y = cursorY;
    state.placedImages.push_back(std::move(placed));

    const float rowHeight = std::max(blockHeight, narrowTextHeight);
    cursorY += rowHeight + kImageBlockGapAfter;
    cursorPos += narrowCharCount;
  }

  state.contentEndY = cursorY;

  const float totalHeightFloat = state.contentEndY + 200.0f;
  state.totalHeight =
      std::max(height, static_cast<uint32_t>(std::ceil(totalHeightFloat)));

  state.actualWidth = width;

  state.result.width = state.actualWidth;
  state.result.height = state.totalHeight;
  state.remainingHeight = state.totalHeight;
  state.currentOffsetY = 0;

  for (const auto &p : state.placedImages) {
    ImageRegion region;
    region.x = p.x;
    region.y = p.y;
    region.width = p.width;
    region.height = p.height;
    state.result.images.push_back(region);
  }

  // --- 区画をまたがずグローバル位置[pos, pos+length)を完全に含む区画を探す ---
  auto findOwningSegment =
      [&](size_t pos, size_t length) -> const WikiTextSegment * {
    for (const auto &seg : state.textSegments) {
      if (pos >= seg.textStart && pos + length <= seg.textStart + seg.textLength) {
        return &seg;
      }
    }
    return nullptr;
  };

  // 見出し位置をピクセル座標として記録（節境界の罫線描画・将来の地形連動に使用）
  for (const auto &h : headingSpans) {
    const WikiTextSegment *seg = findOwningSegment(h.pos, h.length);
    if (!seg || !seg->layout) continue;

    const size_t localPos = h.pos - seg->textStart;
    DWRITE_TEXT_RANGE range = {static_cast<UINT32>(localPos),
                               static_cast<UINT32>(h.length)};
    UINT32 actualCount = 0;
    seg->layout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                  nullptr, 0, &actualCount);
    if (actualCount == 0) continue;

    std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
    seg->layout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                  metrics.data(), actualCount, &actualCount);
    for (const auto &m : metrics) {
      HeadingRegion headingRegion;
      headingRegion.x = m.left + state.marginX;
      headingRegion.y = m.top + seg->yTop;
      headingRegion.width = m.width;
      headingRegion.height = m.height;
      headingRegion.level = h.level;
      state.result.headings.push_back(headingRegion);
    }
  }

  // リンク位置の解析
  state.linkMatched.assign(links.size(), false);
  for (size_t i = 0; i < links.size(); ++i) {
    const auto &linkPair = links[i];
    if (linkPair.first.empty()) continue;

    bool matched = false;
    size_t pos = state.articleText.find(linkPair.first);
    while (pos != std::wstring::npos) {
      const size_t linkLen = linkPair.first.length();
      const WikiTextSegment *seg = findOwningSegment(pos, linkLen);
      if (seg && seg->layout) {
        const size_t localPos = pos - seg->textStart;
        DWRITE_TEXT_RANGE range = {static_cast<UINT32>(localPos),
                                   static_cast<UINT32>(linkLen)};
        bool isTarget = (linkPair.second == targetPage);

        UINT32 actualCount = 0;
        seg->layout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                      nullptr, 0, &actualCount);
        if (actualCount > 0) {
          std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
          seg->layout->HitTestTextRange(range.startPosition, range.length, 0, 0,
                                        metrics.data(), actualCount, &actualCount);
          for (const auto &m : metrics) {
            LinkRegion reg;
            reg.targetPage = linkPair.second;
            reg.x = m.left + state.marginX;
            reg.y = m.top + seg->yTop;
            reg.width = m.width;
            reg.height = m.height;
            reg.isTarget = isTarget;
            state.result.links.push_back(reg);
            matched = true;
          }
        }
      }
      pos = state.articleText.find(linkPair.first, pos + linkPair.first.length());
    }
    state.linkMatched[i] = matched;
  }

  state.started = true;
  return true;
}


} // namespace graphics
