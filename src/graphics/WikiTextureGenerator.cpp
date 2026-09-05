/**
 * @file WikiTextureGenerator.cpp
 * @brief Wikipedia記事テキストからD3D11テクスチャを生成する実装
 */

#include "WikiTextureGenerator.h"
#include "../core/Logger.h"
#include <algorithm>
#include <cmath>
#include <d2d1_1.h>
#include <mutex>
#include <wincodec.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

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
    size_t actualEnd = isLast ? len : lineEnd;
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

/**
 * @brief プロセス共有のWICファクトリを取得する（初回のみ生成）。
 */
Microsoft::WRL::ComPtr<IWICImagingFactory> GetWicFactory() {
  static std::once_flag flag;
  static Microsoft::WRL::ComPtr<IWICImagingFactory> factory;

  std::call_once(flag, []() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory));
  });

  return factory;
}

} // namespace

bool DecodeWikiImageFromMemory(const std::string &bytes,
                               std::vector<uint8_t> &outPixelsBGRA,
                               uint32_t &outWidth, uint32_t &outHeight) {
  if (bytes.empty()) {
    return false;
  }

  auto factory = GetWicFactory();
  if (!factory) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  HRESULT hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return false;
  }

  hr = stream->InitializeFromMemory(
      reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())),
      static_cast<DWORD>(bytes.size()));
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                        WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    return false;
  }

  UINT width = 0, height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    return false;
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return false;
  }

  outWidth = width;
  outHeight = height;
  outPixelsBGRA.assign(static_cast<size_t>(width) * height * 4, 0);
  hr = converter->CopyPixels(nullptr, width * 4,
                             static_cast<UINT>(outPixelsBGRA.size()),
                             outPixelsBGRA.data());
  if (FAILED(hr)) {
    return false;
  }

  // 透過PNG（旗・アイコン等でよくある透明背景）を白背景へ合成し、完全不透明化する。
  // 予乗算アルファのまま透明部分を残すと、アルファブレンドをしない描画経路
  // （3Dの看板など）で透明部分が黒として焼き込まれ、暗く/灰色くなってしまう。
  for (size_t i = 0; i + 3 < outPixelsBGRA.size(); i += 4) {
    const uint8_t alpha = outPixelsBGRA[i + 3];
    if (alpha < 255) {
      const int inv = 255 - alpha;
      outPixelsBGRA[i + 0] = static_cast<uint8_t>(std::min(255, outPixelsBGRA[i + 0] + inv));
      outPixelsBGRA[i + 1] = static_cast<uint8_t>(std::min(255, outPixelsBGRA[i + 1] + inv));
      outPixelsBGRA[i + 2] = static_cast<uint8_t>(std::min(255, outPixelsBGRA[i + 2] + inv));
      outPixelsBGRA[i + 3] = 255;
    }
  }

  return true;
}

bool WikiTextureGenerator::Initialize(ID3D11Device *device) {
  if (!device) {
    LOG_ERROR("WikiTexGen", "D3D11 Device is null");
    return false;
  }
  m_d3dDevice = device;

  HRESULT hr;

  // D2D1.1ファクトリの生成
  D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
  options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options,
                         m_d2dFactory.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create D2D1Factory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // DirectWriteファクトリの生成
  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown **>(m_dwriteFactory.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create DWriteFactory (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // DXGIデバイスの取得
  ComPtr<IDXGIDevice> dxgiDevice;
  hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to get DXGI Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // D2Dデバイスの生成
  hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create D2D Device (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // D2Dデバイスコンテキストの生成
  hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                        &m_d2dContext);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen",
              "Failed to create D2D DeviceContext (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  // タイトル描画用のテキストフォーマットの生成
  hr = m_dwriteFactory->CreateTextFormat(
      L"Georgia", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 128.0f, L"ja-JP", &m_titleFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create title TextFormat");
    return false;
  }

  // 本文用 - 2倍サイズ
  hr = m_dwriteFactory->CreateTextFormat(
      L"Meiryo", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 64.0f, L"ja-JP", &m_bodyFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create body TextFormat");
    return false;
  }

  // 画像キャプション用（本文よりやや小さめ）
  hr = m_dwriteFactory->CreateTextFormat(
      L"Meiryo", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 40.0f, L"ja-JP", &m_captionFormat);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create caption TextFormat");
    return false;
  }

  LOG_INFO("WikiTexGen", "Initialized successfully");
  return true;
}

void WikiTextureGenerator::Shutdown() {
  m_offscreenBitmap.Reset();
  m_offscreenTexture.Reset();
  m_titleFormat.Reset();
  m_bodyFormat.Reset();
  m_captionFormat.Reset();
  m_d2dContext.Reset();
  m_d2dDevice.Reset();
  m_dwriteFactory.Reset();
  m_d2dFactory.Reset();
  m_d3dDevice.Reset();
}

bool WikiTextureGenerator::CreateOffscreenTarget(uint32_t width,
                                                 uint32_t height) {
  // レガシー互換用（単一タイル生成で使用する場合）
  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // D2D互換形式
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; // D2Dと共有

  HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr,
                                            m_offscreenTexture.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  ComPtr<IDXGISurface> dxgiSurface;
  hr = m_offscreenTexture.As(&dxgiSurface);
  if (FAILED(hr)) {
    return false;
  }

  D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  hr = m_d2dContext->CreateBitmapFromDxgiSurface(
      dxgiSurface.Get(), &bitmapProps, &m_offscreenBitmap);
  if (FAILED(hr)) {
    return false;
  }

  return true;
}

ComPtr<ID2D1Bitmap> WikiTextureGenerator::CreateBitmapFromPixels(
    const uint8_t *bgra, uint32_t width, uint32_t height) {
  if (!bgra || width == 0 || height == 0) {
    return nullptr;
  }

  D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap> bitmap;
  HRESULT hr = m_d2dContext->CreateBitmap(D2D1::SizeU(width, height), bgra,
                                          width * 4, props, &bitmap);
  if (FAILED(hr)) {
    LOG_ERROR("WikiTexGen", "Failed to create bitmap from pixels (HRESULT: {:08X})",
              static_cast<uint32_t>(hr));
    return nullptr;
  }
  return bitmap;
}

bool WikiTextureGenerator::BeginGenerateTexture(
    WikiTextureGenerationState &state, const std::wstring &title,
    const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height,
    std::vector<PendingWikiImage> pendingImages) {

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
    size_t segEnd = (pi < placements.size()) ? placements[pi].insertionPos
                                             : state.articleText.size();
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
    const float blockHeight =
        imgH + (captionLayout ? (kCaptionPad + captionMetrics.height) : 0.0f);
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

    const float narrowTextHeight =
        (narrowCharCount > 0)
            ? makeSegment(cursorPos, narrowCharCount, cursorY, narrowWidth)
            : 0.0f;

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

bool WikiTextureGenerator::GenerateNextTile(WikiTextureGenerationState &state) {
  if (!state.started || state.completed) return true;

  // 描画負荷をフレーム分散するために最大タイル高さを半分に調整
  const uint32_t kMaxTileHeight = 512;
  uint32_t tileH = std::min(state.remainingHeight, kMaxTileHeight);
  uint32_t width = state.actualWidth;

  // ブラシ色
  D2D1::ColorF colBg(1.0f, 1.0f, 1.0f, 1.0f);
  D2D1::ColorF colText(0.125f, 0.129f, 0.133f, 1.0f);
  D2D1::ColorF colLink(0.023f, 0.270f, 0.678f, 1.0f);
  D2D1::ColorF colTarget(0.647f, 0.506f, 0.0f, 1.0f);
  D2D1::ColorF colBorder(0.8f, 0.8f, 0.8f, 1.0f);
  D2D1::ColorF colLinkBack(0.9f, 0.95f, 1.0f, 1.0f);
  D2D1::ColorF colTargetBack(1.0f, 0.98f, 0.8f, 1.0f);
  D2D1::ColorF colTargetGlow(0.647f, 0.506f, 0.0f, 0.8f);

  ComPtr<ID3D11Texture2D> tex;
  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = width;
  texDesc.Height = tileH;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &tex);
  if (FAILED(hr)) return true;

  ComPtr<IDXGISurface> dxgiSurface;
  tex.As(&dxgiSurface);

  D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap1> bmp;
  hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bmpProps, &bmp);
  if (FAILED(hr)) return true;

  m_d2dContext->SetTarget(bmp.Get());
  m_d2dContext->BeginDraw();
  m_d2dContext->Clear(colBg);

  if (!state.brushesInitialized) {
    m_d2dContext->CreateSolidColorBrush(colText, &state.bText);
    m_d2dContext->CreateSolidColorBrush(colLink, &state.bLink);
    m_d2dContext->CreateSolidColorBrush(colTarget, &state.bTarget);
    m_d2dContext->CreateSolidColorBrush(colBorder, &state.bBorder);
    m_d2dContext->CreateSolidColorBrush(colLinkBack, &state.bBackLink);
    m_d2dContext->CreateSolidColorBrush(colTargetBack, &state.bBackTarget);
    m_d2dContext->CreateSolidColorBrush(colTargetGlow, &state.bGlow);

    // リンク文字列の装飾（太字・下線・色）を、その文字列を含む区画へ適用する
    auto findOwningSegmentMutable =
        [&](size_t pos, size_t length) -> WikiTextSegment * {
      for (auto &seg : state.textSegments) {
        if (pos >= seg.textStart && pos + length <= seg.textStart + seg.textLength) {
          return &seg;
        }
      }
      return nullptr;
    };

    for (size_t i = 0; i < state.links.size(); ++i) {
      size_t pos = state.articleText.find(state.links[i].first);
      while (pos != std::wstring::npos) {
        const size_t linkLen = state.links[i].first.length();
        WikiTextSegment *seg = findOwningSegmentMutable(pos, linkLen);
        if (seg && seg->layout) {
          const size_t localPos = pos - seg->textStart;
          DWRITE_TEXT_RANGE range = {static_cast<UINT32>(localPos),
                                     static_cast<UINT32>(linkLen)};
          bool isTarget = (state.links[i].second == state.targetPage);
          if (isTarget) {
            seg->layout->SetDrawingEffect(state.bTarget.Get(), range);
          } else {
            seg->layout->SetDrawingEffect(state.bLink.Get(), range);
          }
          seg->layout->SetUnderline(TRUE, range);
          seg->layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        }
        pos = state.articleText.find(state.links[i].first, pos + 1);
      }
    }
    state.brushesInitialized = true;
  }

  D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Translation(0.0f, -static_cast<float>(state.currentOffsetY));
  m_d2dContext->SetTransform(transform);

  m_d2dContext->DrawTextLayout(D2D1::Point2F(state.marginX, state.titleTopY), state.titleLayout.Get(), state.bText.Get());
  m_d2dContext->DrawLine(D2D1::Point2F(state.marginX, state.separatorY), D2D1::Point2F(width - state.marginX, state.separatorY), state.bBorder.Get(), 2.0f);

  for (const auto &l : state.result.links) {
    D2D1_RECT_F r = D2D1::RectF(l.x, l.y, l.x + l.width, l.y + l.height);
    if (l.isTarget) {
      m_d2dContext->FillRectangle(r, state.bBackTarget.Get());
    } else {
      m_d2dContext->FillRectangle(r, state.bBackLink.Get());
    }
    if (l.isTarget) m_d2dContext->DrawRectangle(r, state.bGlow.Get(), 3.0f);
  }

  // 本文（float画像の左右で幅の異なる区画に分けて描画）
  for (const auto &seg : state.textSegments) {
    if (!seg.layout) continue;
    m_d2dContext->DrawTextLayout(D2D1::Point2F(state.marginX, seg.yTop), seg.layout.Get(), state.bText.Get());
  }

  // Wikipedia風の画像枠＋キャプション
  for (const auto &img : state.placedImages) {
    D2D1_RECT_F dest = D2D1::RectF(img.x, img.y, img.x + img.width, img.y + img.height);
    m_d2dContext->DrawBitmap(img.bitmap.Get(), &dest);
    m_d2dContext->DrawRectangle(dest, state.bBorder.Get(), 1.5f);
    if (img.captionLayout) {
      m_d2dContext->DrawTextLayout(
          D2D1::Point2F(img.x, img.y + img.height + 8.0f),
          img.captionLayout.Get(), state.bText.Get());
    }
  }

  // 見出し下部にWikipedia風の区切り罫線を描画
  for (const auto &h : state.result.headings) {
    float lineY = h.y + h.height + 6.0f;
    m_d2dContext->DrawLine(D2D1::Point2F(state.marginX, lineY),
                          D2D1::Point2F(width - state.marginX, lineY),
                          state.bBorder.Get(), 1.5f);
  }

  float seeAlsoY = state.contentEndY + 60.0f;
  int unmatchedCount = 0;
  float linkSpacing = 60.0f;
  for (size_t i = 0; i < state.links.size(); ++i) {
    if (!state.linkMatched[i]) {
      float lx = state.marginX + (unmatchedCount % 3) * 220.0f;
      float ly = seeAlsoY + (unmatchedCount / 3) * linkSpacing;
      if (ly > state.totalHeight - 50.0f) break;

      D2D1_RECT_F linkRect = D2D1::RectF(lx, ly, lx + 200.0f, ly + 50.0f);
      bool isTarget = (state.links[i].second == state.targetPage);
      if (isTarget) {
        m_d2dContext->FillRectangle(linkRect, state.bTarget.Get());
      } else {
        m_d2dContext->FillRectangle(linkRect, state.bLink.Get());
      }

      ComPtr<ID2D1SolidColorBrush> bWhite;
      m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &bWhite);
      m_d2dContext->DrawTextW(state.links[i].first.c_str(), static_cast<UINT32>(state.links[i].first.length()), m_bodyFormat.Get(), linkRect, bWhite.Get());

      unmatchedCount++;
      if (state.currentOffsetY == 0) {
        LinkRegion reg;
        reg.targetPage = state.links[i].second;
        reg.x = lx; reg.y = ly; reg.width = 200; reg.height = 50; reg.isTarget = isTarget;
        state.result.links.push_back(reg);
      }
    }
  }

  m_d2dContext->EndDraw();

  ComPtr<ID3D11ShaderResourceView> srv;
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  m_d3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, &srv);

  WikiTextureResult::Tile tile;
  tile.texture = tex;
  tile.srv = srv;
  tile.width = width;
  tile.height = tileH;
  tile.offsetY = static_cast<float>(state.currentOffsetY);
  state.result.tiles.push_back(tile);

  state.remainingHeight -= tileH;
  state.currentOffsetY += tileH;

  if (state.remainingHeight == 0) {
    if (!state.result.tiles.empty()) {
      state.result.texture = state.result.tiles[0].texture;
      state.result.srv = state.result.tiles[0].srv;
    }
    m_d2dContext->SetTarget(nullptr);
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    state.completed = true;
    return true;
  }

  return false;
}

WikiTextureResult WikiTextureGenerator::GenerateTexture(
    const std::wstring &title, const std::wstring &articleText,
    const std::vector<std::pair<std::wstring, std::string>> &links,
    const std::string &targetPage, uint32_t width, uint32_t height,
    std::vector<PendingWikiImage> pendingImages) {

  WikiTextureGenerationState state;
  if (!BeginGenerateTexture(state, title, articleText, links, targetPage,
                           width, height, std::move(pendingImages))) {
    return WikiTextureResult();
  }

  while (!GenerateNextTile(state)) {
    // タイル生成ループを継続
  }

  return std::move(state.result);
}

} // namespace graphics
