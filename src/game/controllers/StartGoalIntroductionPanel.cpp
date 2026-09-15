/**
 * @file StartGoalIntroductionPanel.cpp
 * @brief StartGoalIntroductionPanel の実装
*/

#include "StartGoalIntroductionPanel.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../utils/CourseIntroductionRules.h"
#include "../utils/StartGoalIntroductionRules.h"
#include "../utils/UIConstants.h"
#include "hud/HudStyles.h"
#include <algorithm>
#include <cmath>

namespace game::controllers {

namespace {

using Timing = game::utils::StartGoalIntroductionTiming;

// 1280x720 の仮想解像度でのレイアウト
constexpr float kScreenWidth = 1280.0f;
constexpr float kCardWidth = 486.0f;
constexpr float kStartCardX = 88.0f;
constexpr float kGoalCardX = kScreenWidth - kStartCardX - kCardWidth;
constexpr float kCardPadding = 28.0f;
constexpr float kContentWidth = kCardWidth - kCardPadding * 2.0f;

// カードを置ける縦方向の範囲（見出しの下〜操作案内の上）
constexpr float kCardAreaTop = 172.0f;
constexpr float kHintGap = 18.0f;
constexpr float kHintHeight = 24.0f;
constexpr float kCardAreaBottom = 620.0f - kHintGap - kHintHeight;
constexpr float kMaxCardHeight = kCardAreaBottom - kCardAreaTop;
constexpr float kMaxCenteringOffset = 40.0f;

// カード内の縦方向の並び（カード上端からの距離）
constexpr float kLabelY = 22.0f;
constexpr float kLabelHeight = 22.0f;
constexpr float kTitleY = 48.0f;
constexpr float kMinTitleHeight = 36.0f;
constexpr float kTitleToRule = 8.0f;
constexpr float kRuleToBody = 12.0f;
constexpr float kBodyToUrl = 16.0f;
constexpr float kUrlHeight = 20.0f;
constexpr float kBottomPadding = 22.0f;
/** @brief 記事名と本文以外が占める高さの合計です。*/
constexpr float kCardFixedHeight =
    kTitleY + kTitleToRule + kRuleToBody + kBodyToUrl + kUrlHeight +
    kBottomPadding;
/** @brief 実測誤差でテキスト末尾が切れないよう、描画枠に足す余白です。*/
constexpr float kTextBoxSlack = 4.0f;

constexpr float kThumbnailMaxWidth = 140.0f;
constexpr float kThumbnailMaxHeight = 172.0f;
constexpr float kThumbnailFramePadding = 5.0f;
constexpr float kThumbnailGap = 12.0f;
constexpr float kSlideDistance = 18.0f;

constexpr int kLayerVeil = 26;
constexpr int kLayerCard = 30;
constexpr int kLayerCardDecoration = 31;
constexpr int kLayerCardContent = 32;

graphics::TextStyle MakeCardLabelStyle(const DirectX::XMFLOAT4 &color) {
  graphics::TextStyle style = graphics::TextStyle::CardLabel();
  style.fontSize = 15.0f;
  style.color = color;
  return style;
}

graphics::TextStyle MakeBodyStyle() {
  graphics::TextStyle style = graphics::TextStyle::BrowserURL();
  style.fontSize = 15.0f;
  style.color = game::ui::kColorTextPrimary;
  return style;
}

} // namespace

void StartGoalIntroductionPanel::Initialize(core::GameContext &ctx,
                                            const Content &content) {
  Shutdown(ctx);
  m_elapsed = 0.0f;
  m_fadeOutElapsed = -1.0f;

  // 背後のコースをうっすら残す紙面色のベール
  ecs::Entity veil = AddText(ctx, Group::Veil, 0.0f, 0.0f, kLayerVeil);
  if (auto *text = ctx.world.Get<components::UIText>(veil)) {
    text->fullScreenCover = true;
    text->style.bgColor = game::ui::kColorBgDark;
    text->style.bgColor.w = 0.62f;
  }

  ecs::Entity headerLabel =
      AddText(ctx, Group::Header, 0.0f, kScreenWidth, kLayerCardContent);
  PlaceText(ctx, headerLabel, 86.0f, 24.0f);
  if (auto *text = ctx.world.Get<components::UIText>(headerLabel)) {
    text->text = L"WIKIGOLF  ·  TODAY'S ROUND";
    text->style = MakeCardLabelStyle(game::ui::kColorAccent);
    text->style.align = graphics::TextAlign::Center;
  }

  ecs::Entity headline =
      AddText(ctx, Group::Header, 0.0f, kScreenWidth, kLayerCardContent);
  PlaceText(ctx, headline, 110.0f, 44.0f);
  if (auto *text = ctx.world.Get<components::UIText>(headline)) {
    text->text = L"スタート記事から、リンクをたどってゴール記事をめざそう";
    text->style = graphics::TextStyle::BrowserURL();
    text->style.fontFamily = "Kiwi Maru Medium";
    text->style.fontSize = 25.0f;
    text->style.color = game::ui::kColorTextPrimary;
    text->style.align = graphics::TextAlign::Center;
  }

  CreateArticleCard(ctx, m_startCard, Group::StartCard, kStartCardX, false,
                    content.startTitle);
  m_startCard.abstractText = content.startAbstract.empty()
                                 ? L"（概要はありません）"
                                 : content.startAbstract;

  CreateArticleCard(ctx, m_goalCard, Group::GoalCard, kGoalCardX, true,
                    content.goalTitle);
  m_goalCard.abstractText = L"記事の概要を読み込んでいます…";
  if (auto *body = ctx.world.Get<components::UIText>(m_goalCard.body)) {
    body->style.color = game::ui::kColorTextSub;
  }

  // ゴールカード: 代表画像があれば Wikipedia のインフォボックス風に右上へ置く
  if (content.goalThumbnail) {
    const float aspect = std::max(content.goalThumbnailAspect, 0.1f);
    float width = kThumbnailMaxWidth;
    float height = width / aspect;
    if (height > kThumbnailMaxHeight) {
      height = kThumbnailMaxHeight;
      width = height * aspect;
    }
    m_goalCard.thumbnailWidth = width;
    m_goalCard.thumbnailHeight = height;
    const float imageX = kGoalCardX + kCardWidth - kCardPadding - width;
    m_goalCard.thumbnailFrame =
        AddText(ctx, Group::GoalCard, imageX - kThumbnailFramePadding,
                width + kThumbnailFramePadding * 2.0f, kLayerCardDecoration);
    if (auto *text =
            ctx.world.Get<components::UIText>(m_goalCard.thumbnailFrame)) {
      text->style.bgColor = game::ui::kColorWhite;
      text->style.borderWidth = game::ui::kBorderWidthThin;
      text->style.borderColor = game::ui::kColorBorder;
    }
    m_goalCard.thumbnail =
        AddImage(ctx, Group::GoalCard, imageX, width, height,
                 kLayerCardContent, content.goalThumbnail);
  }

  // カード間: リンクをたどる矢印と最短手数
  const float gapX = kStartCardX + kCardWidth;
  const float gapWidth = kGoalCardX - gapX;
  m_arrowCaption =
      AddText(ctx, Group::Arrow, gapX, gapWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(m_arrowCaption)) {
    text->text = content.hops > 0 ? L"SHORTEST" : L"LINKS";
    text->style = MakeCardLabelStyle(game::ui::kColorTextSub);
    text->style.align = graphics::TextAlign::Center;
  }
  m_arrow = AddText(ctx, Group::Arrow, gapX, gapWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(m_arrow)) {
    text->text = L"→";
    text->style = graphics::TextStyle::BrowserURL();
    text->style.fontSize = 52.0f;
    text->style.color = game::ui::kColorAccent;
    text->style.align = graphics::TextAlign::Center;
  }
  if (content.hops > 0) {
    m_hops = AddText(ctx, Group::Arrow, gapX + 18.0f, gapWidth - 36.0f,
                     kLayerCardContent);
    if (auto *text = ctx.world.Get<components::UIText>(m_hops)) {
      text->text = L"HOP " + std::to_wstring(content.hops);
      text->style = graphics::TextStyle::CardValue();
      text->style.fontFamily = "Share Tech Mono";
      text->style.fontSize = 17.0f;
      text->style.align = graphics::TextAlign::Center;
      text->style.valign = graphics::TextVAlign::Middle;
      hud::ApplyRowStyle(text->style);
    }
  }

  m_hint = AddText(ctx, Group::Hint, 0.0f, kScreenWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(m_hint)) {
    text->text = L"クリック / Enter で次へ";
    text->style = graphics::TextStyle::ShotPanelLabel();
    text->style.fontSize = 16.0f;
    text->style.align = graphics::TextAlign::Center;
  }

  m_initialized = true;
  Layout(ctx);
  ApplyAnimation(ctx);
}

void StartGoalIntroductionPanel::CreateArticleCard(
    core::GameContext &ctx, Card &card, Group group, float x, bool isGoal,
    const std::wstring &title) {
  const DirectX::XMFLOAT4 &accent =
      isGoal ? game::ui::kColorSpecial : game::ui::kColorAccent;
  const float contentX = x + kCardPadding;
  card.x = x;

  card.panel = AddText(ctx, group, x, kCardWidth, kLayerCard);
  if (auto *text = ctx.world.Get<components::UIText>(card.panel)) {
    hud::ApplySurfaceStyle(text->style);
  }

  // カード上端のアクセント線（スタート=リンク青、ゴール=秀逸な記事の金）
  card.accentBar = AddText(ctx, group, x, kCardWidth, kLayerCardDecoration);
  if (auto *text = ctx.world.Get<components::UIText>(card.accentBar)) {
    text->style.bgColor = accent;
    text->style.cornerRadius = game::ui::kRadiusChip;
  }

  card.label = AddText(ctx, group, contentX, kContentWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(card.label)) {
    text->text = isGoal ? L"★ GOAL  ·  ゴール記事" : L"START  ·  スタート記事";
    text->style = MakeCardLabelStyle(accent);
  }

  card.title = AddText(ctx, group, contentX, kContentWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(card.title)) {
    text->text = title;
    // 記事名は紙面見出しの明朝系。ゴール記事だけ金色で強調する
    text->style = graphics::TextStyle::GoalHighlight();
    if (!isGoal) {
      text->style.color = game::ui::kColorTextPrimary;
    }
    text->style.fontSize =
        game::utils::SelectStartGoalTitleFontSize(title.size());
  }

  // Wikipedia の記事名下線に倣った極細の区切り線
  card.rule =
      AddText(ctx, group, contentX, kContentWidth, kLayerCardDecoration);
  if (auto *text = ctx.world.Get<components::UIText>(card.rule)) {
    text->style.bgColor = game::ui::kColorBorder;
  }

  card.body = AddText(ctx, group, contentX, kContentWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(card.body)) {
    text->style = MakeBodyStyle();
  }

  card.url = AddText(ctx, group, contentX, kContentWidth, kLayerCardContent);
  if (auto *text = ctx.world.Get<components::UIText>(card.url)) {
    text->text = game::utils::FormatWikipediaArticleUrl(title);
    text->style = graphics::TextStyle::BrowserSub();
    text->style.fontFamily = "Share Tech Mono";
    text->style.fontSize = 13.0f;
    text->style.color = game::ui::kColorAccent;
  }
}

void StartGoalIntroductionPanel::SetGoalExtract(
    core::GameContext &ctx, const std::wstring &rawExtract) {
  if (!m_initialized) {
    return;
  }
  const std::wstring formatted = game::utils::FormatCourseAbstract(rawExtract);
  m_goalCard.abstractText =
      formatted.empty() ? L"（概要を取得できませんでした）" : formatted;
  if (auto *body = ctx.world.Get<components::UIText>(m_goalCard.body)) {
    body->style.color = game::ui::kColorTextPrimary;
  }
  Layout(ctx);
  ApplyAnimation(ctx);
}

float StartGoalIntroductionPanel::MeasureHeight(
    core::GameContext &ctx, const std::wstring &text,
    const graphics::TextStyle &style, float width) const {
  if (text.empty()) {
    return 0.0f;
  }
  if (ctx.textRenderer) {
    const float measured =
        ctx.textRenderer->MeasureTextHeight(text, style, width);
    if (measured >= 0.0f) {
      return measured;
    }
  }
  // 計測できない環境では全角文字幅で行数を見積もる
  const float charactersPerLine =
      std::max(1.0f, std::floor(width / style.fontSize));
  const float lines =
      std::ceil(static_cast<float>(text.size()) / charactersPerLine);
  return lines * style.fontSize * 1.4f;
}

float StartGoalIntroductionPanel::BodyWidth(const Card &card) const {
  if (card.thumbnailWidth <= 0.0f) {
    return kContentWidth;
  }
  return kContentWidth - card.thumbnailWidth -
         kThumbnailFramePadding * 2.0f - kThumbnailGap;
}

void StartGoalIntroductionPanel::Layout(core::GameContext &ctx) {
  auto *startTitle = ctx.world.Get<components::UIText>(m_startCard.title);
  auto *goalTitle = ctx.world.Get<components::UIText>(m_goalCard.title);
  auto *startBody = ctx.world.Get<components::UIText>(m_startCard.body);
  auto *goalBody = ctx.world.Get<components::UIText>(m_goalCard.body);
  if (!startTitle || !goalTitle || !startBody || !goalBody) {
    return;
  }

  // 記事名: 両カードで区切り線の高さを揃えるため、高い方に合わせる
  const float titleHeight = std::max(
      {kMinTitleHeight,
       MeasureHeight(ctx, startTitle->text, startTitle->style, kContentWidth),
       MeasureHeight(ctx, goalTitle->text, goalTitle->style, kContentWidth)});
  const float thumbnailFrameHeight =
      m_goalCard.thumbnailHeight > 0.0f
          ? m_goalCard.thumbnailHeight + kThumbnailFramePadding * 2.0f
          : 0.0f;
  const float maxBodyHeight =
      std::max(kMaxCardHeight - kCardFixedHeight - titleHeight,
               thumbnailFrameHeight);

  // 本文: 両カードとも枠に収まる文字サイズ・文字数を選ぶ
  graphics::TextStyle bodyStyle = startBody->style;
  const auto bodyTextFor = [](const Card &card, std::size_t maxCharacters) {
    return game::utils::FormatCourseAbstract(card.abstractText, maxCharacters);
  };
  const auto measureBodies = [&](float fontSize, std::size_t maxCharacters) {
    bodyStyle.fontSize = fontSize;
    return std::max(
        MeasureHeight(ctx, bodyTextFor(m_startCard, maxCharacters), bodyStyle,
                      BodyWidth(m_startCard)),
        MeasureHeight(ctx, bodyTextFor(m_goalCard, maxCharacters), bodyStyle,
                      BodyWidth(m_goalCard)));
  };
  const game::utils::StartGoalBodyFit fit =
      game::utils::FitStartGoalBody(measureBodies, maxBodyHeight);

  startBody->text = bodyTextFor(m_startCard, fit.maxCharacters);
  goalBody->text = bodyTextFor(m_goalCard, fit.maxCharacters);
  startBody->style.fontSize = fit.fontSize;
  goalBody->style.fontSize = fit.fontSize;
  startBody->width = BodyWidth(m_startCard);
  goalBody->width = BodyWidth(m_goalCard);
  const float bodyHeight = std::min(
      maxBodyHeight,
      std::max(measureBodies(fit.fontSize, fit.maxCharacters),
               thumbnailFrameHeight));

  const float cardHeight = kCardFixedHeight + titleHeight + bodyHeight;
  const float cardY =
      kCardAreaTop +
      std::clamp((kMaxCardHeight - cardHeight) * 0.5f, 0.0f,
                 kMaxCenteringOffset);
  const float ruleY = cardY + kTitleY + titleHeight + kTitleToRule;
  const float bodyY = ruleY + kRuleToBody;
  const float urlY = bodyY + bodyHeight + kBodyToUrl;

  for (Card *card : {&m_startCard, &m_goalCard}) {
    PlaceText(ctx, card->panel, cardY, cardHeight);
    PlaceText(ctx, card->accentBar, cardY, 4.0f);
    PlaceText(ctx, card->label, cardY + kLabelY, kLabelHeight);
    PlaceText(ctx, card->title, cardY + kTitleY, titleHeight + kTextBoxSlack);
    PlaceText(ctx, card->rule, ruleY, 1.0f);
    PlaceText(ctx, card->body, bodyY, bodyHeight + kTextBoxSlack);
    PlaceText(ctx, card->url, urlY, kUrlHeight);
    if (card->thumbnailFrame != UINT32_MAX) {
      PlaceText(ctx, card->thumbnailFrame, bodyY, thumbnailFrameHeight);
      PlaceText(ctx, card->thumbnail, bodyY + kThumbnailFramePadding,
                card->thumbnailHeight);
    }
  }

  const float centerY = cardY + cardHeight * 0.5f;
  PlaceText(ctx, m_arrowCaption, centerY - 60.0f, 22.0f);
  PlaceText(ctx, m_arrow, centerY - 42.0f, 64.0f);
  if (m_hops != UINT32_MAX) {
    PlaceText(ctx, m_hops, centerY + 28.0f, 30.0f);
  }
  PlaceText(ctx, m_hint, cardY + cardHeight + kHintGap, kHintHeight);
}

void StartGoalIntroductionPanel::PlaceText(core::GameContext &ctx,
                                           ecs::Entity entity, float y,
                                           float height) {
  for (Element &element : m_elements) {
    if (element.entity != entity) {
      continue;
    }
    element.baseY = y;
    if (element.isImage) {
      if (auto *image = ctx.world.Get<components::UIImage>(entity)) {
        image->height = height;
      }
    } else if (auto *text = ctx.world.Get<components::UIText>(entity)) {
      text->height = height;
    }
    return;
  }
}

bool StartGoalIntroductionPanel::Update(core::GameContext &ctx, float dt,
                                        bool advanceRequested) {
  if (!m_initialized) {
    return true;
  }

  if (m_fadeOutElapsed < 0.0f) {
    m_elapsed += dt;
    if (advanceRequested) {
      if (game::utils::ResolveStartGoalAdvance(m_elapsed) ==
          game::utils::StartGoalAdvance::RevealAll) {
        m_elapsed = Timing::FullyRevealedTime();
      } else {
        m_fadeOutElapsed = 0.0f;
      }
    } else if (m_elapsed >= Timing::kAutoAdvanceTime) {
      m_fadeOutElapsed = 0.0f;
    }
  } else {
    m_fadeOutElapsed += dt;
    m_elapsed += dt;
  }

  ApplyAnimation(ctx);
  return m_fadeOutElapsed >= Timing::kFadeOutDuration;
}

void StartGoalIntroductionPanel::ApplyAnimation(core::GameContext &ctx) {
  const float fadeOut =
      m_fadeOutElapsed < 0.0f
          ? 1.0f
          : 1.0f - std::clamp(m_fadeOutElapsed / Timing::kFadeOutDuration,
                              0.0f, 1.0f);

  const auto delayOf = [](Group group) {
    switch (group) {
    case Group::Veil:
      return 0.0f;
    case Group::Header:
      return Timing::kHeaderDelay;
    case Group::StartCard:
      return Timing::kStartCardDelay;
    case Group::Arrow:
      return Timing::kArrowDelay;
    case Group::GoalCard:
      return Timing::kGoalCardDelay;
    case Group::Hint:
      return Timing::kHintDelay;
    }
    return 0.0f;
  };

  for (const Element &element : m_elements) {
    const float reveal = game::utils::CalculateStartGoalRevealProgress(
        m_elapsed, delayOf(element.group));
    float opacity = reveal * fadeOut;
    if (element.group == Group::Hint) {
      // 操作案内は控えめに明滅させる
      const float pulse =
          0.65f + 0.35f * std::cos((m_elapsed - Timing::kHintDelay) * 3.0f);
      opacity *= pulse;
    }
    const float slide = element.group == Group::Veil
                            ? 0.0f
                            : (1.0f - reveal) * kSlideDistance;
    const bool visible = opacity > 0.001f;

    if (element.isImage) {
      if (auto *image = ctx.world.Get<components::UIImage>(element.entity)) {
        image->opacity = opacity;
        image->y = element.baseY + slide;
        image->visible = visible;
      }
    } else if (auto *text =
                   ctx.world.Get<components::UIText>(element.entity)) {
      text->opacity = opacity;
      text->y = element.baseY + slide;
      text->visible = visible;
    }
  }
}

ecs::Entity StartGoalIntroductionPanel::AddText(core::GameContext &ctx,
                                                Group group, float x,
                                                float width, int layer) {
  ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &text = ctx.world.Add<components::UIText>(entity);
  text.x = x;
  text.width = width;
  text.visible = false;
  text.opacity = 0.0f;
  text.layer = layer;
  m_elements.push_back({entity, group, 0.0f, false});
  return entity;
}

ecs::Entity StartGoalIntroductionPanel::AddImage(
    core::GameContext &ctx, Group group, float x, float width, float height,
    int layer, ID3D11ShaderResourceView *texture) {
  ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &image = ctx.world.Add<components::UIImage>(entity);
  image.textureSRV = texture;
  image.x = x;
  image.width = width;
  image.height = height;
  image.visible = false;
  image.opacity = 0.0f;
  image.layer = layer;
  m_elements.push_back({entity, group, 0.0f, true});
  return entity;
}

void StartGoalIntroductionPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_elements.clear();
  m_startCard = Card{};
  m_goalCard = Card{};
  m_arrowCaption = m_arrow = m_hops = m_hint = UINT32_MAX;
  m_initialized = false;
}

} // namespace game::controllers
