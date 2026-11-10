/**
 * @file TutorialGuidePanel.cpp
 * @brief チュートリアルの案内パネル（目次の形）の配置・色・クリック
*/

#include "TutorialGuidePanel.h"
#include "../../../core/GameContext.h"
#include "../../../core/Input.h"
#include "../../../ecs/World.h"
#include "../../../graphics/TextRenderer.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>

namespace game::controllers::hud {

namespace {

using game::components::UIText;
using game::utils::TutorialKeyJoiner;
using game::utils::TutorialSpotlight;

// 風の表示（左上）とクラブ表示（左下）の間に収める。全体マップの操作一覧
// （画面中央・左端 x=400）とも重ならない幅にしている。
constexpr float kPanelX = 20.0f;
constexpr float kPanelY = 188.0f;
constexpr float kPanelWidth = 360.0f;
constexpr float kPaddingX = 14.0f;
constexpr float kPaddingY = 8.0f;
constexpr float kContentX = kPanelX + kPaddingX;
constexpr float kContentWidth = kPanelWidth - kPaddingX * 2.0f;
/** @brief これより下へ伸びるときは目次を前後1章に縮める（クラブ表示の上端の手前）。*/
constexpr float kMaxBottom = game::ui::kClubPanelY - 12.0f;

constexpr float kNumberColumn = 22.0f;   ///< 章番号の列の幅
constexpr float kSectionIndent = 22.0f;  ///< 開いた節の字下げ
constexpr float kSectionNumberWidth = 34.0f;
constexpr float kToggleWidth = 64.0f;

constexpr float kTitleFontSize = 15.0f;
constexpr float kToggleFontSize = 13.0f;
constexpr float kChapterFontSize = 15.0f;
constexpr float kHeadlineFontSize = 21.0f;
constexpr float kBodyFontSize = 15.0f;
constexpr float kSmallFontSize = 13.0f;
constexpr float kHintFontSize = 12.0f;

constexpr float kEnterSeconds = 0.18f;

const char *const kSerifFont = "Yu Mincho"; // Windows 標準の游明朝
const char *const kSansFont = "Meiryo";

// Wikipedia のリンク色。訪問済み（済んだ章・できた操作）は紫、押している間は濃い青。
const DirectX::XMFLOAT4 kColorVisitedLink = {0.475f, 0.361f, 0.698f, 1.0f};
const DirectX::XMFLOAT4 kColorActiveLink = {0.165f, 0.294f, 0.553f, 1.0f};
const DirectX::XMFLOAT4 kColorSectionRule = {0.784f, 0.800f, 0.820f, 1.0f};

/** @brief 文字数から横幅を見積もります（全角は1文字、半角は0.6文字ぶん）。*/
float EstimateTextWidth(const std::wstring &text, float fontSize) {
  float width = 0.0f;
  for (const wchar_t character : text) {
    width += (character < 0x80 ? 0.6f : 1.0f) * fontSize;
  }
  return width;
}

/** @brief 折り返したときの高さを返します。描画側で測れないときは見積もります。*/
float MeasureHeight(core::GameContext &ctx, const std::wstring &text,
                    const graphics::TextStyle &style, float width) {
  if (text.empty()) {
    return 0.0f;
  }
  if (ctx.textRenderer) {
    const float measured =
        ctx.textRenderer->MeasureTextHeight(text, style, width);
    if (measured > 0.0f) {
      return measured;
    }
  }
  const float lines = std::max(
      1.0f, std::ceil(EstimateTextWidth(text, style.fontSize) /
                      std::max(width, 1.0f)));
  return lines * style.fontSize * 1.5f;
}

float EaseOutCubic(float t) {
  const float rest = 1.0f - std::clamp(t, 0.0f, 1.0f);
  return 1.0f - rest * rest * rest;
}

const wchar_t *JoinerText(TutorialKeyJoiner joiner) {
  switch (joiner) {
  case TutorialKeyJoiner::Then:
    return L"→";
  case TutorialKeyJoiner::Plus:
    return L"＋";
  case TutorialKeyJoiner::Or:
    return L"または";
  default:
    return L"";
  }
}

graphics::TextStyle MakeTextStyle(const char *font, float fontSize,
                                  const DirectX::XMFLOAT4 &color,
                                  graphics::TextAlign align) {
  graphics::TextStyle style;
  style.fontFamily = font;
  style.fontSize = fontSize;
  style.color = color;
  style.align = align;
  return style;
}

struct SpotlightRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

SpotlightRect GetSpotlightRect(TutorialSpotlight spotlight) {
  switch (spotlight) {
  case TutorialSpotlight::Minimap:
    return {game::ui::kMinimapX - 4.0f, game::ui::kMinimapY - 4.0f,
            game::ui::kMinimapWidth + 8.0f, game::ui::kMinimapHeight + 8.0f};
  case TutorialSpotlight::ClubPanel:
    return {6.0f, game::ui::kClubPanelY + 2.0f, game::ui::kClubItemW + 8.0f,
            76.0f};
  case TutorialSpotlight::ShotRuler:
    return {game::ui::kShotRulerX - 8.0f, game::ui::kShotRulerY - 26.0f,
            game::ui::kShotRulerWidth + 16.0f,
            game::ui::kShotRulerHeight + 34.0f};
  default:
    return {};
  }
}

} // namespace

ecs::Entity TutorialGuidePanel::CreateText(core::GameContext &ctx,
                                           int layerOffset) {
  const ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &text = ctx.world.Add<UIText>(entity);
  text.layer = game::ui::kLayerOverlay + layerOffset;
  text.visible = false;
  m_texts.push_back(entity);
  return entity;
}

void TutorialGuidePanel::Initialize(core::GameContext &ctx) {
  Shutdown(ctx);
  using graphics::TextAlign;

  // Wikipedia の目次と同じ、角のない淡いグレーの面と細い線。
  m_background = CreateText(ctx, 0);
  auto &background = ctx.world.Get<UIText>(m_background)->style;
  background.bgColor = game::ui::kColorBgPanel;
  background.borderColor = game::ui::kColorBorder;
  background.borderWidth = game::ui::kBorderWidthThin;
  background.cornerRadius = 0.0f;

  const auto text = [&](int layer, const char *font, float size,
                        const DirectX::XMFLOAT4 &color, TextAlign align) {
    const ecs::Entity entity = CreateText(ctx, layer);
    ctx.world.Get<UIText>(entity)->style =
        MakeTextStyle(font, size, color, align);
    return entity;
  };

  m_title = text(1, kSansFont, kTitleFontSize, game::ui::kColorTextPrimary,
                 TextAlign::Center);
  m_toggle = text(1, kSansFont, kToggleFontSize, game::ui::kColorAccent,
                  TextAlign::Right);

  for (auto &row : m_chapters) {
    row.number = text(1, kSansFont, kChapterFontSize,
                      game::ui::kColorTextPrimary, TextAlign::Left);
    row.title = text(1, kSansFont, kChapterFontSize, game::ui::kColorAccent,
                     TextAlign::Left);
  }
  m_moreAbove = text(1, kSansFont, kChapterFontSize, game::ui::kColorTextSub,
                     TextAlign::Left);
  m_moreBelow = text(1, kSansFont, kChapterFontSize, game::ui::kColorTextSub,
                     TextAlign::Left);

  m_sectionRule = CreateText(ctx, 1);
  ctx.world.Get<UIText>(m_sectionRule)->style.bgColor = kColorSectionRule;

  m_sectionNumber = text(1, kSansFont, kSmallFontSize, game::ui::kColorTextSub,
                         TextAlign::Left);
  m_headline = text(1, kSerifFont, kHeadlineFontSize,
                    game::ui::kColorTextPrimary, TextAlign::Left);
  m_detail = text(1, kSansFont, kBodyFontSize, game::ui::kColorTextPrimary,
                  TextAlign::Left);
  for (auto &row : m_keyRows) {
    row.joiner = text(1, kSansFont, kSmallFontSize, game::ui::kColorTextSub,
                      TextAlign::Left);
    row.key = text(1, kSansFont, kBodyFontSize, game::ui::kColorAccent,
                   TextAlign::Left);
    row.caption = text(1, kSansFont, kBodyFontSize,
                       game::ui::kColorTextPrimary, TextAlign::Left);
  }
  m_lookAt = text(1, kSansFont, kSmallFontSize, game::ui::kColorTextSub,
                  TextAlign::Left);
  m_hint = text(1, kSansFont, kHintFontSize, game::ui::kColorTextSub,
                TextAlign::Right);
  m_collapsedLine = text(1, kSansFont, kBodyFontSize,
                         game::ui::kColorTextPrimary, TextAlign::Left);

  // 指したいHUD部品は、細いリンク色の線で囲み、角に脚注番号を付ける。
  m_spotlight = CreateText(ctx, -1);
  auto &spotlight = ctx.world.Get<UIText>(m_spotlight)->style;
  spotlight.borderWidth = 1.5f;
  spotlight.borderColor = game::ui::kColorAccent;
  spotlight.cornerRadius = 0.0f;

  m_spotlightTag = text(-1, kSansFont, kSmallFontSize, game::ui::kColorAccent,
                        TextAlign::Center);
  auto &tag = ctx.world.Get<UIText>(m_spotlightTag)->style;
  tag.bgColor = game::ui::kColorWhite;
  tag.borderColor = game::ui::kColorBorder;
  tag.borderWidth = game::ui::kBorderWidthThin;
  ctx.world.Get<UIText>(m_spotlightTag)->text = L"[1]";

  m_content = {};
  m_spotlightTarget = TutorialSpotlight::None;
  m_enterTimer = 0.0f;
  m_stepCleared = false;
  m_collapsed = false;
  m_toggleHovered = false;
  m_pressOnPanel = false;
  m_pressOnToggle = false;
  m_capturingPointer = false;
  m_visible = true;
  m_initialized = true;
  Layout(ctx);
}

void TutorialGuidePanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_texts.clear();
  m_placed.clear();
  m_panelRect = {};
  m_toggleRect = {};
  m_capturingPointer = false;
  m_pressOnPanel = false;
  m_initialized = false;
}

void TutorialGuidePanel::SetContent(
    core::GameContext &ctx, const game::utils::TutorialGuideContent &content) {
  if (!m_initialized || content == m_content) {
    return;
  }
  m_content = content;
  m_enterTimer = 0.0f;
  m_stepCleared = false;
  Layout(ctx);
}

void TutorialGuidePanel::SetSpotlight(core::GameContext &ctx,
                                      TutorialSpotlight spotlight) {
  if (m_spotlightTarget == spotlight) {
    return;
  }
  m_spotlightTarget = spotlight;
  UpdateSpotlight(ctx);
}

void TutorialGuidePanel::PlayStepClear(core::GameContext &ctx) {
  if (!m_initialized || m_stepCleared) {
    return;
  }
  m_stepCleared = true;
  Layout(ctx);
}

void TutorialGuidePanel::SetVisible(core::GameContext &ctx, bool visible) {
  if (m_visible == visible) {
    return;
  }
  m_visible = visible;
  if (!visible) {
    m_pressOnPanel = false;
    m_pressOnToggle = false;
    m_capturingPointer = false;
  }
  if (!m_initialized) {
    return;
  }
  if (visible) {
    m_enterTimer = 0.0f;
  }
  Layout(ctx);
}

void TutorialGuidePanel::HideAll(core::GameContext &ctx) {
  for (const ecs::Entity entity : m_texts) {
    if (entity == m_spotlight || entity == m_spotlightTag) {
      continue; // 枠は UpdateSpotlight が受け持つ
    }
    if (auto *text = ctx.world.Get<UIText>(entity)) {
      text->visible = false;
    }
  }
  m_placed.clear();
}

void TutorialGuidePanel::Place(core::GameContext &ctx, ecs::Entity entity,
                               float x, float y, float width, float height) {
  auto *text = ctx.world.Get<UIText>(entity);
  if (!text || width <= 0.0f || height <= 0.0f) {
    return;
  }
  text->x = x;
  text->y = y;
  text->width = width;
  text->height = height;
  text->visible = true;
  m_placed.push_back(entity);
}

void TutorialGuidePanel::Layout(core::GameContext &ctx) {
  HideAll(ctx);
  m_panelRect = {};
  m_toggleRect = {};
  if (!m_visible || m_content.headline.empty()) {
    UpdateSpotlight(ctx);
    return;
  }

  float bottom = 0.0f;
  if (m_collapsed) {
    bottom = LayoutCollapsed(ctx);
  } else {
    bottom = LayoutExpanded(ctx, false);
    if (bottom > kMaxBottom) {
      HideAll(ctx);
      bottom = LayoutExpanded(ctx, true);
    }
  }

  m_panelRect = {kPanelX, kPanelY, kPanelWidth, bottom - kPanelY};
  Place(ctx, m_background, m_panelRect.x, m_panelRect.y, m_panelRect.width,
        m_panelRect.height);
  if (m_collapsed) {
    // 縮めた表示は小さいので、どこを押しても開く。
    m_toggleRect = m_panelRect;
  }

  ApplyColors(ctx);
  ApplyOpacity(ctx);
  UpdateSpotlight(ctx);
}

float TutorialGuidePanel::LayoutExpanded(core::GameContext &ctx,
                                         bool compact) {
  const auto setText = [&](ecs::Entity entity, const std::wstring &value) {
    if (auto *text = ctx.world.Get<UIText>(entity)) {
      text->text = value;
    }
  };
  const auto styleOf = [&](ecs::Entity entity) {
    return ctx.world.Get<UIText>(entity)->style;
  };
  const auto lineHeight = [&](ecs::Entity entity) {
    return MeasureHeight(ctx, L"あ", styleOf(entity), 1000.0f);
  };

  float y = kPanelY + kPaddingY;

  // 題の行：「目次」と［隠す］
  setText(m_title, L"目次");
  const float titleHeight = lineHeight(m_title);
  Place(ctx, m_title, kContentX, y, kContentWidth, titleHeight + 2.0f);
  setText(m_toggle, L"[隠す]");
  const float toggleHeight = lineHeight(m_toggle);
  Place(ctx, m_toggle, kContentX + kContentWidth - kToggleWidth,
        y + (titleHeight - toggleHeight) * 0.5f, kToggleWidth,
        toggleHeight + 2.0f);
  y += titleHeight + 6.0f;
  m_toggleRect = {kPanelX, kPanelY, kPanelWidth, y - kPanelY};

  const int current = std::clamp(m_content.chapterIndex, 0,
                                 game::utils::kTutorialChapterCount - 1);
  int first = 0;
  int last = 0;
  game::utils::GetTutorialTocRange(current, compact, first, last);

  const float rowHeight = lineHeight(m_chapters[0].title) + 3.0f;
  if (first > 0) {
    setText(m_moreAbove, L"…");
    Place(ctx, m_moreAbove, kContentX, y, kNumberColumn, rowHeight);
    y += rowHeight;
  }

  for (int i = first; i <= last; ++i) {
    const auto &row = m_chapters[static_cast<std::size_t>(i)];
    setText(row.number, std::to_wstring(i + 1));
    Place(ctx, row.number, kContentX, y, kNumberColumn, rowHeight);
    setText(row.title, game::utils::GetTutorialChapterTitle(i));
    Place(ctx, row.title, kContentX + kNumberColumn, y,
          kContentWidth - kNumberColumn, rowHeight);
    y += rowHeight;

    if (i != current) {
      continue;
    }

    // いまの章だけを開き、その中の節（いますること）を出す。
    const float itemX = kContentX + kSectionIndent;
    const float itemWidth = kContentWidth - kSectionIndent;
    const float top = y + 2.0f;
    float itemY = top;

    setText(m_sectionNumber,
            m_stepCleared || m_content.celebrate
                ? std::wstring(L"✓")
                : game::utils::GetTutorialSectionNumber(
                      m_content.chapterIndex, m_content.pageInChapter));
    const float headlineWidth = itemWidth - kSectionNumberWidth;
    setText(m_headline, m_content.headline);
    const float headlineHeight = MeasureHeight(
        ctx, m_content.headline, styleOf(m_headline), headlineWidth);
    const float headlineLine = lineHeight(m_headline);
    const float numberLine = lineHeight(m_sectionNumber);
    Place(ctx, m_sectionNumber, itemX,
          itemY + (headlineLine - numberLine) * 0.5f, kSectionNumberWidth,
          numberLine + 2.0f);
    Place(ctx, m_headline, itemX + kSectionNumberWidth, itemY, headlineWidth,
          headlineHeight + 4.0f);
    itemY += headlineHeight + 4.0f;

    if (!m_content.detail.empty()) {
      setText(m_detail, m_content.detail);
      const float detailHeight = MeasureHeight(
          ctx, m_content.detail, styleOf(m_detail), itemWidth);
      Place(ctx, m_detail, itemX, itemY, itemWidth, detailHeight + 4.0f);
      itemY += detailHeight + 6.0f;
    }

    // 使うキー：つなぎ（灰）・キー名（リンクの青）・起きること（黒）を1行ずつ。
    const std::size_t keyCount =
        std::min(m_content.keys.size(), kMaxKeyRows);
    const float bodyLine = lineHeight(m_keyRows[0].key);
    const float smallLine = lineHeight(m_keyRows[0].joiner);
    for (std::size_t k = 0; k < keyCount; ++k) {
      const auto &key = m_content.keys[k];
      const auto &row = m_keyRows[k];
      float x = itemX;
      const std::wstring joiner = k > 0 ? JoinerText(key.joiner) : L"";
      if (!joiner.empty()) {
        const float joinerWidth =
            EstimateTextWidth(joiner, kSmallFontSize) + 8.0f;
        setText(row.joiner, joiner);
        Place(ctx, row.joiner, x, itemY + (bodyLine - smallLine) * 0.5f,
              joinerWidth, smallLine + 2.0f);
        x += joinerWidth;
      }
      const float keyWidth = EstimateTextWidth(key.key, kBodyFontSize) + 12.0f;
      setText(row.key, key.key);
      Place(ctx, row.key, x, itemY, keyWidth, bodyLine + 2.0f);
      x += keyWidth;
      float rowBottom = bodyLine;
      if (!key.caption.empty()) {
        const float captionWidth = itemX + itemWidth - x;
        setText(row.caption, key.caption);
        const float captionHeight = MeasureHeight(
            ctx, key.caption, styleOf(row.caption), captionWidth);
        Place(ctx, row.caption, x, itemY, captionWidth, captionHeight + 2.0f);
        rowBottom = std::max(rowBottom, captionHeight);
      }
      itemY += rowBottom + 3.0f;
    }

    // 指しているHUD部品の名前。部品の角の「[1]」と対応させる。
    const std::wstring lookAt =
        game::utils::GetTutorialSpotlightLabel(m_content.spotlight);
    if (!lookAt.empty()) {
      setText(m_lookAt, L"見る場所：" + lookAt + L" [1]");
      Place(ctx, m_lookAt, itemX, itemY + 1.0f, itemWidth, smallLine + 2.0f);
      itemY += smallLine + 4.0f;
    }

    Place(ctx, m_sectionRule, kContentX + 9.0f, top, 1.0f, itemY - top);
    y = itemY + 6.0f;
  }

  if (last < game::utils::kTutorialChapterCount - 1 && compact) {
    setText(m_moreBelow, L"…");
    Place(ctx, m_moreBelow, kContentX, y, kNumberColumn, rowHeight);
    y += rowHeight;
  }

  if (!m_content.hint.empty()) {
    setText(m_hint, m_content.hint);
    const float hintHeight = lineHeight(m_hint);
    y += 2.0f;
    Place(ctx, m_hint, kContentX, y, kContentWidth, hintHeight + 2.0f);
    y += hintHeight + 2.0f;
  }
  return y + kPaddingY;
}

float TutorialGuidePanel::LayoutCollapsed(core::GameContext &ctx) {
  const auto lineHeight = [&](ecs::Entity entity) {
    return MeasureHeight(ctx, L"あ", ctx.world.Get<UIText>(entity)->style,
                         1000.0f);
  };
  float y = kPanelY + kPaddingY;

  if (auto *title = ctx.world.Get<UIText>(m_title)) {
    title->text = L"目次";
  }
  const float titleHeight = lineHeight(m_title);
  Place(ctx, m_title, kContentX, y, kContentWidth, titleHeight + 2.0f);
  if (auto *toggle = ctx.world.Get<UIText>(m_toggle)) {
    toggle->text = L"[表示]";
  }
  const float toggleHeight = lineHeight(m_toggle);
  Place(ctx, m_toggle, kContentX + kContentWidth - kToggleWidth,
        y + (titleHeight - toggleHeight) * 0.5f, kToggleWidth,
        toggleHeight + 2.0f);
  y += titleHeight + 4.0f;

  // 縮めても、いますることだけは読めるように1行残す。
  const std::wstring prefix =
      m_stepCleared || m_content.celebrate
          ? std::wstring(L"✓ ")
          : game::utils::GetTutorialSectionNumber(m_content.chapterIndex,
                                                  m_content.pageInChapter) +
                L"　";
  const std::wstring line = prefix + m_content.headline;
  auto *collapsed = ctx.world.Get<UIText>(m_collapsedLine);
  if (!collapsed) {
    return y + kPaddingY;
  }
  collapsed->text = line;
  const float lineHeightValue =
      MeasureHeight(ctx, line, collapsed->style, kContentWidth);
  Place(ctx, m_collapsedLine, kContentX, y, kContentWidth,
        lineHeightValue + 2.0f);
  y += lineHeightValue + 2.0f;
  return y + kPaddingY;
}

void TutorialGuidePanel::ApplyOpacity(core::GameContext &ctx) {
  // 内容が替わるときは短いフェードだけにする（横へ滑らせたり弾ませたりしない）。
  const float eased = EaseOutCubic(m_enterTimer / kEnterSeconds);
  for (const ecs::Entity entity : m_placed) {
    if (auto *text = ctx.world.Get<UIText>(entity)) {
      text->opacity = entity == m_background ? 1.0f : eased;
    }
  }
}

void TutorialGuidePanel::ApplyColors(core::GameContext &ctx) {
  const auto setColor = [&](ecs::Entity entity,
                            const DirectX::XMFLOAT4 &color) {
    if (auto *text = ctx.world.Get<UIText>(entity)) {
      text->style.color = color;
    }
  };

  const int current = m_content.chapterIndex;
  for (int i = 0; i < game::utils::kTutorialChapterCount; ++i) {
    const auto &row = m_chapters[static_cast<std::size_t>(i)];
    DirectX::XMFLOAT4 color = game::ui::kColorAccent; // これからの章
    if (m_content.celebrate || i < current) {
      color = kColorVisitedLink; // 済んだ章
    } else if (i == current) {
      color = game::ui::kColorTextPrimary; // いまの章
    }
    setColor(row.title, color);
  }

  DirectX::XMFLOAT4 numberColor = game::ui::kColorTextSub;
  if (m_content.celebrate) {
    numberColor = game::ui::kColorSpecial;
  } else if (m_stepCleared) {
    numberColor = game::ui::kColorSuccess;
  }
  setColor(m_sectionNumber, numberColor);

  // できた操作は、訪問済みのリンクと同じ紫に変える。
  const DirectX::XMFLOAT4 keyColor =
      m_stepCleared ? kColorVisitedLink : game::ui::kColorAccent;
  for (const auto &row : m_keyRows) {
    setColor(row.key, keyColor);
  }

  setColor(m_toggle, m_toggleHovered ? kColorActiveLink
                                     : game::ui::kColorAccent);
}

void TutorialGuidePanel::UpdateSpotlight(core::GameContext &ctx) {
  auto *spotlight = ctx.world.Get<UIText>(m_spotlight);
  auto *tag = ctx.world.Get<UIText>(m_spotlightTag);
  if (!spotlight || !tag) {
    return;
  }
  const SpotlightRect rect = GetSpotlightRect(m_spotlightTarget);
  if (!m_visible || rect.width <= 0.0f || m_content.headline.empty()) {
    spotlight->visible = false;
    tag->visible = false;
    return;
  }
  spotlight->x = rect.x;
  spotlight->y = rect.y;
  spotlight->width = rect.width;
  spotlight->height = rect.height;
  spotlight->opacity = 1.0f;
  spotlight->visible = true;

  // 番号札は枠の左上の外に出す。画面の上端に近い部品では枠の内側に入れる。
  constexpr float kTagWidth = 30.0f;
  constexpr float kTagHeight = 20.0f;
  const bool roomAbove = rect.y >= kTagHeight + 2.0f;
  tag->x = roomAbove ? rect.x : rect.x + 3.0f;
  tag->y = roomAbove ? rect.y - kTagHeight - 1.0f : rect.y + 3.0f;
  tag->width = kTagWidth;
  tag->height = kTagHeight;
  tag->opacity = 1.0f;
  tag->visible = true;
}

void TutorialGuidePanel::UpdatePointer(core::GameContext &ctx) {
  m_capturingPointer = false;
  if (!m_visible || m_panelRect.width <= 0.0f) {
    m_pressOnPanel = false;
    m_pressOnToggle = false;
    if (m_toggleHovered) {
      m_toggleHovered = false;
      ApplyColors(ctx);
    }
    return;
  }

  const auto mouse = ctx.input.GetMousePosition();
  const float mouseX = static_cast<float>(mouse.x);
  const float mouseY = static_cast<float>(mouse.y);
  const bool insidePanel = m_panelRect.Contains(mouseX, mouseY);
  const bool insideToggle = m_toggleRect.Contains(mouseX, mouseY);

  bool anyDown = false;
  bool anyHeld = false;
  for (int button = 0; button < 3; ++button) {
    anyDown = anyDown || ctx.input.GetMouseButtonDown(button);
    anyHeld = anyHeld || ctx.input.GetMouseButton(button);
  }

  if (anyDown && insidePanel && !m_pressOnPanel) {
    m_pressOnPanel = true;
    m_pressOnToggle = insideToggle && ctx.input.GetMouseButtonDown(0);
  }

  if (m_pressOnPanel) {
    // 押してから離すまで、ほかの操作へ渡さない（パネル上でのクリックで
    // 打ったり、視点が回ったりしないようにする）。
    m_capturingPointer = true;
    if (ctx.input.GetMouseButtonUp(0) && m_pressOnToggle && insideToggle) {
      m_collapsed = !m_collapsed;
      m_pressOnToggle = false;
      Layout(ctx);
    }
    if (!anyHeld) {
      m_pressOnPanel = false;
      m_pressOnToggle = false;
    }
  }

  const bool hovered = m_toggleRect.Contains(mouseX, mouseY);
  if (hovered != m_toggleHovered) {
    m_toggleHovered = hovered;
    ApplyColors(ctx);
  }
}

void TutorialGuidePanel::Update(core::GameContext &ctx) {
  if (!m_initialized) {
    return;
  }
  UpdatePointer(ctx);
  if (!m_visible) {
    return;
  }
  if (m_enterTimer < kEnterSeconds) {
    m_enterTimer = std::min(m_enterTimer + ctx.dt, kEnterSeconds);
    ApplyOpacity(ctx);
  }
}

} // namespace game::controllers::hud
