/**
 * @file GoalRouteEffect.cpp
 * @brief GoalRouteEffect の実装
*/

#include "GoalRouteEffect.h"
#include "ArticlePaperEffect.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../../graphics/Mesh.h"
#include "../../graphics/TextRenderer.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../scenes/HoleVisualRules.h"
#include "../utils/ProceduralFlag.h"
#include <algorithm>
#include <cmath>

namespace game::controllers {

namespace {

constexpr int kLayerLine = 30;
constexpr int kLayerCup = 31;
constexpr int kLayerText = 32;

constexpr float kVirtualWidth = 1280.0f;
constexpr float kTextMargin = 40.0f;      ///< 画面の左右で文字を寄せる限界
constexpr float kLineThickness = 3.0f;
constexpr float kDashLength = 10.0f;      ///< 最短手数が未計算のときの点線
constexpr float kDashGap = 9.0f;
constexpr float kBallRadius = 12.0f;      ///< 仮想解像度でのボールの半径
constexpr float kLabelWidth = 160.0f;
constexpr float kLabelHeight = 26.0f;
constexpr float kStartTitleSize = 30.0f;
constexpr float kGoalTitleLift = 14.0f;   ///< 記事名が下から上がってくる距離

/// 旗をどれだけ紙から浮かせるか（旗の寸法で）。布のなびきが紙に刺さらない距離
constexpr float kFlagForward = 0.6f;
/// 布を少しだけ奥へ振り、なびきの凹凸が見えるようにする（ラジアン）
constexpr float kClothViewYaw = 0.35f;

const DirectX::XMFLOAT3 kBandColor = {0.957f, 0.965f, 0.957f};
const DirectX::XMFLOAT3 kRuleColor = {0.635f, 0.663f, 0.694f};
constexpr float kBandForward = 0.01f;
constexpr float kRuleForward = 0.02f;
constexpr float kRuleThickness = 1.5f;

const DirectX::XMFLOAT4 kInkColor = {0.12f, 0.16f, 0.14f, 1.0f};
const DirectX::XMFLOAT4 kSubInkColor = {0.33f, 0.35f, 0.36f, 1.0f};
const DirectX::XMFLOAT4 kLineColor = {0.17f, 0.21f, 0.19f, 1.0f};
const DirectX::XMFLOAT4 kCupColor = {0.17f, 0.165f, 0.15f, 1.0f};
const DirectX::XMFLOAT4 kGoalInkColor = {0.72f, 0.15f, 0.11f, 1.0f};

graphics::TextStyle MakeRouteTextStyle(float fontSize,
                                       const DirectX::XMFLOAT4 &color,
                                       graphics::TextAlign align) {
  graphics::TextStyle style = graphics::TextStyle::BrowserURL();
  style.fontFamily = "Kiwi Maru Medium";
  style.fontSize = fontSize;
  style.color = color;
  style.align = align;
  style.valign = graphics::TextVAlign::Middle;
  return style;
}

/** @brief 残り手数の文字色。旗の色を紙の上で読める濃さにしたものです。*/
DirectX::XMFLOAT4 RemainingLabelColor(int remaining) {
  if (remaining == 0) {
    return kGoalInkColor;
  }
  return {0.12f, 0.35f, 0.56f, 1.0f};
}

/** @brief 指定幅に1行で収まるかを測ります。測れないときは収まるものとします。*/
bool FitsOnOneLine(core::GameContext &ctx, const std::wstring &text,
                   const graphics::TextStyle &style, float width) {
  if (!ctx.textRenderer || text.empty()) {
    return true;
  }
  const float height = ctx.textRenderer->MeasureTextHeight(text, style, width);
  return height < 0.0f || height <= style.fontSize * 1.6f;
}

float EaseOutCubic(float t) {
  const float rest = 1.0f - std::clamp(t, 0.0f, 1.0f);
  return 1.0f - rest * rest * rest;
}

} // namespace

void GoalRouteEffect::Spawn(core::GameContext &ctx, const Content &content,
                            ArticlePaperEffect &paper) {
  Shutdown(ctx);
  if (!paper.IsActive()) {
    return;
  }
  m_active = true;

  // 記事の文字を隠して、図版のような欄を敷く
  paper.AddBand(ctx, utils::kRouteBandTop, utils::kRouteBandBottom, kBandColor,
                kBandForward);
  paper.AddBand(ctx, utils::kRouteBandTop, utils::kRouteBandTop + kRuleThickness,
                kRuleColor, kRuleForward);
  paper.AddBand(ctx, utils::kRouteBandBottom - kRuleThickness,
                utils::kRouteBandBottom, kRuleColor, kRuleForward);

  // 線（未計算なら点線）
  m_dashedLine = content.hops <= 0;
  const float lineLength = utils::kRouteEndX - utils::kRouteStartX;
  if (m_dashedLine) {
    for (float start = 0.0f; start < lineLength; start += kDashLength + kDashGap) {
      m_lineSegments.push_back(AddText(ctx, utils::kRouteStartX + start,
                                       utils::kRouteCupY - kLineThickness * 0.5f,
                                       std::min(kDashLength, lineLength - start),
                                       kLineThickness, kLayerLine));
      m_lineSegmentStarts.push_back(start);
    }
  } else {
    m_lineSegments.push_back(AddText(ctx, utils::kRouteStartX,
                                     utils::kRouteCupY - kLineThickness * 0.5f,
                                     0.0f, kLineThickness, kLayerLine));
    m_lineSegmentStarts.push_back(0.0f);
  }
  for (ecs::Entity segment : m_lineSegments) {
    if (auto *text = ctx.world.Get<components::UIText>(segment)) {
      text->style.bgColor = kLineColor;
    }
  }

  CreateBall(ctx, paper);
  for (const utils::GoalRouteFlag &route : utils::BuildGoalRouteFlags(content.hops)) {
    CreateFlag(ctx, route, paper);
  }
  CreateLabels(ctx, content);
}

void GoalRouteEffect::CreateBall(core::GameContext &ctx,
                                 ArticlePaperEffect &paper) {
  const float metersPerPixel =
      paper.ResolveScreenPoint(utils::kRouteStartX, utils::kRouteCupY, 0.0f)
          .metersPerPixel;
  const float forward = kBallRadius * metersPerPixel;
  const ArticlePaperEffect::Anchor anchor = paper.ResolveScreenPoint(
      utils::kRouteStartX, utils::kRouteCupY - kBallRadius, forward);
  m_ballAnchor = anchor.local;
  m_ballMetersPerPixel = anchor.metersPerPixel;

  m_ball = m_entityOwner.Create(ctx.world);
  auto &transform = ctx.world.Add<components::Transform>(m_ball);
  const auto mesh = ctx.resource.LoadMesh("Assets/models/golfball.glb");
  float nativeRadius = 1.0f;
  if (auto *ballMesh = ctx.resource.GetMesh(mesh)) {
    nativeRadius = std::max(ballMesh->GetBounds().Radius, 0.0001f);
  }
  const float scale = kBallRadius * anchor.metersPerPixel / nativeRadius;
  transform.scale = {scale, scale, scale};

  // コースのボールと同じメッシュ・シェーダー・法線マップ
  auto &renderer = ctx.world.Add<components::MeshRenderer>(m_ball);
  renderer.mesh = mesh;
  renderer.shader = ctx.resource.LoadShader(
      "GolfBall", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/GolfBallPS.hlsl");
  renderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
  renderer.normalMapSRV =
      ctx.resource.LoadTextureSRV("Assets/models/golfball_n.png");
  renderer.hasNormalMap = static_cast<bool>(renderer.normalMapSRV);
  // 紙が倒れるときに旗と一緒に薄れるよう、不透明度をディザーで表す
  renderer.usesDitherFade = true;
  renderer.isVisible = false;
  paper.Attach(m_ball, m_ballAnchor);
}

void GoalRouteEffect::CreateFlag(core::GameContext &ctx,
                                 const utils::GoalRouteFlag &route,
                                 ArticlePaperEffect &paper) {
  RouteFlag flag;
  flag.route = route;
  const bool isTarget = route.remaining == 0;

  // 距離による色分けはせず、ゴールの旗だけを強調する
  utils::ProceduralFlagOptions options;
  options.large = isTarget;
  options.createParticles = false;
  options.animationWeight = isTarget ? 1.0f : 0.72f;
  const DirectX::XMFLOAT4 color =
      scenes::HoleVisualRules::GetColor(isTarget, -1);

  const float paperMetersPerPixel =
      paper.ResolveScreenPoint(route.x, utils::kRouteCupY, 0.0f).metersPerPixel;
  const float forward =
      kFlagForward * utils::kRoutePixelsPerFlagMeter * paperMetersPerPixel;
  const ArticlePaperEffect::Anchor anchor =
      paper.ResolveScreenPoint(route.x, utils::kRouteCupY, forward);
  flag.anchor = anchor.local;
  flag.metersPerFlagMeter =
      utils::kRoutePixelsPerFlagMeter * anchor.metersPerPixel;
  flag.cupWidth = 0.52f * utils::GetProceduralFlagSize(options.large) *
                  utils::kRoutePixelsPerFlagMeter;

  const auto result =
      utils::CreateProceduralFlag(ctx, {0.0f, 0.0f, 0.0f}, color, options);
  static constexpr PartRole kRoles[] = {PartRole::Pole, PartRole::Cap,
                                        PartRole::Cloth, PartRole::Seam};
  for (std::size_t i = 0; i < result.allEntities.size(); ++i) {
    const ecs::Entity entity = result.allEntities[i];
    m_entityOwner.Track(entity);
    FlagPart part;
    part.entity = entity;
    part.role = i < std::size(kRoles) ? kRoles[i] : PartRole::Seam;
    if (const auto *transform = ctx.world.Get<components::Transform>(entity)) {
      part.offset = transform->position;
      part.scale = transform->scale;
    }
    if (auto *renderer = ctx.world.Get<components::MeshRenderer>(entity)) {
      renderer->isVisible = false;
      // 画面の手前にあるので、遠距離の描画打ち切りを使わない
      renderer->maxDrawDistance = 0.0f;
      // 紙が倒れるときや布がなびくとき、紙に埋まらないよう手前に描く
      renderer->drawsInFront = true;
    }
    if (part.role == PartRole::Cloth) {
      if (const auto *holeFlag = ctx.world.Get<components::HoleFlag>(entity)) {
        flag.clothAmplitude = holeFlag->amplitude;
      }
    }
    paper.Attach(entity, flag.anchor);
    flag.parts.push_back(part);
  }

  flag.cup = AddText(ctx, route.x, utils::kRouteCupY, 0.0f, 0.0f, kLayerCup);
  if (auto *text = ctx.world.Get<components::UIText>(flag.cup)) {
    text->style.bgColor = kCupColor;
  }

  const std::wstring step = utils::FormatRouteStepLabel(route.step);
  float labelY = utils::kRouteCupY + 22.0f;
  if (!step.empty()) {
    flag.stepLabel = AddText(ctx, route.x - kLabelWidth * 0.5f, labelY,
                             kLabelWidth, kLabelHeight, kLayerText);
    if (auto *text = ctx.world.Get<components::UIText>(flag.stepLabel)) {
      text->text = step;
      text->style =
          MakeRouteTextStyle(17.0f, kSubInkColor, graphics::TextAlign::Center);
    }
    labelY += 24.0f;
  }
  flag.remainingLabel = AddText(ctx, route.x - kLabelWidth * 0.5f, labelY,
                                kLabelWidth, kLabelHeight, kLayerText);
  if (auto *text = ctx.world.Get<components::UIText>(flag.remainingLabel)) {
    text->text = utils::FormatRouteRemainingLabel(route.remaining);
    text->style = MakeRouteTextStyle(17.0f, RemainingLabelColor(route.remaining),
                                     graphics::TextAlign::Center);
    text->style.fontFamily = "Kiwi Maru Medium";
  }

  m_flags.push_back(std::move(flag));
}

void GoalRouteEffect::CreateLabels(core::GameContext &ctx,
                                   const Content &content) {
  const std::wstring detail =
      utils::FormatGoalNoticeDetail(content.par, content.hops);
  if (!detail.empty()) {
    m_detail = AddText(ctx, kTextMargin, utils::kRouteBandTop + 12.0f, 600.0f,
                       28.0f, kLayerText);
    if (auto *text = ctx.world.Get<components::UIText>(m_detail)) {
      text->text = detail;
      text->style =
          MakeRouteTextStyle(17.0f, kSubInkColor, graphics::TextAlign::Left);
    }
  }

  // スタートの記事名。ボールの真上に収まらなければ左端から書く
  graphics::TextStyle startStyle = MakeRouteTextStyle(
      kStartTitleSize, kInkColor, graphics::TextAlign::Center);
  float startX = utils::kRouteStartX - (utils::kRouteStartX - kTextMargin);
  float startWidth = (utils::kRouteStartX - kTextMargin) * 2.0f;
  if (!FitsOnOneLine(ctx, content.startTitle, startStyle, startWidth)) {
    startStyle.align = graphics::TextAlign::Left;
    startWidth = utils::kRouteEndX - kTextMargin * 3.0f - startX;
  }
  m_startTitle = AddText(ctx, startX, utils::kRouteCupY - 74.0f, startWidth,
                         40.0f, kLayerText);
  if (auto *text = ctx.world.Get<components::UIText>(m_startTitle)) {
    text->text = content.startTitle;
    text->style = startStyle;
  }
  m_startLabel = AddText(ctx, utils::kRouteStartX - kLabelWidth * 0.5f,
                         utils::kRouteCupY + 22.0f, kLabelWidth, kLabelHeight * 2,
                         kLayerText);
  if (auto *text = ctx.world.Get<components::UIText>(m_startLabel)) {
    text->text = L"スタート";
    if (content.hops > 0) {
      text->text += L"\n" + utils::FormatRouteRemainingLabel(content.hops);
    }
    text->style =
        MakeRouteTextStyle(17.0f, kSubInkColor, graphics::TextAlign::Center);
  }

  // ゴールの記事名は赤い旗の上。旗の真上に収まらなければ右端へ寄せる
  const float goalSize = utils::GetProceduralFlagSize(true);
  const float capTop = utils::kRouteCupY -
                       (utils::GetProceduralFlagPoleHeight(true) + 0.17f * goalSize) *
                           utils::kRoutePixelsPerFlagMeter;
  graphics::TextStyle goalStyle = MakeRouteTextStyle(
      utils::SelectRouteGoalTitleFontSize(content.goalTitle.size()), kInkColor,
      graphics::TextAlign::Center);
  const float goalCenter = utils::kRouteEndX + 20.0f;
  const float rightLimit = kVirtualWidth - kTextMargin;
  float goalX = goalCenter - (rightLimit - goalCenter);
  float goalWidth = (rightLimit - goalCenter) * 2.0f;
  if (!FitsOnOneLine(ctx, content.goalTitle, goalStyle, goalWidth)) {
    goalStyle.align = graphics::TextAlign::Right;
    goalX = kVirtualWidth * 0.35f;
    goalWidth = rightLimit - goalX;
  }
  const float goalHeight = goalStyle.fontSize * 1.4f;
  m_goalTitleY = capTop - 8.0f - goalHeight;
  m_goalTitle = AddText(ctx, goalX, m_goalTitleY, goalWidth, goalHeight,
                        kLayerText);
  if (auto *text = ctx.world.Get<components::UIText>(m_goalTitle)) {
    text->text = content.goalTitle;
    text->style = goalStyle;
  }
  m_goalHeadingY = m_goalTitleY - 24.0f;
  m_goalHeading = AddText(ctx, goalX, m_goalHeadingY, goalWidth, 24.0f,
                          kLayerText);
  if (auto *text = ctx.world.Get<components::UIText>(m_goalHeading)) {
    text->text = L"ゴール";
    text->style = MakeRouteTextStyle(17.0f, kGoalInkColor, goalStyle.align);
  }
}

void GoalRouteEffect::Update(core::GameContext &ctx, float elapsed,
                             const utils::ArticleArrivalTiming &timing,
                             ArticlePaperEffect &paper) {
  if (!m_active) {
    return;
  }
  const float opacity = utils::CalculateRouteOpacity(elapsed, timing);

  // 線を伸ばす
  const float lineLength = utils::kRouteEndX - utils::kRouteStartX;
  const float head = lineLength * utils::CalculateRouteLineProgress(elapsed);
  for (std::size_t i = 0; i < m_lineSegments.size(); ++i) {
    auto *text = ctx.world.Get<components::UIText>(m_lineSegments[i]);
    if (!text) {
      continue;
    }
    const float start = m_lineSegmentStarts[i];
    if (m_dashedLine) {
      text->visible = head > start && opacity > 0.001f;
    } else {
      text->width = head;
      text->visible = head > 0.5f && opacity > 0.001f;
    }
    text->opacity = opacity;
  }

  // ボールが落ちて弾む
  if (auto *renderer = ctx.world.Get<components::MeshRenderer>(m_ball)) {
    renderer->isVisible = elapsed >= utils::kRouteBallAppear;
    DirectX::XMFLOAT3 local = m_ballAnchor;
    // 上へ浮かせる量も、紙の奥行きでの 1 ピクセルの長さで換算する
    local.y += utils::CalculateRouteBallLift(elapsed) * m_ballMetersPerPixel;
    paper.SetAttachedLocal(m_ball, local);
  }

  // 風向きでなびく向きが変わらないよう、布の向きを打ち消す
  float windYaw = 0.0f;
  if (const auto *state = ctx.world.GetGlobal<components::GolfGameState>()) {
    windYaw = std::atan2(state->windDirection.y, state->windDirection.x);
  }
  for (RouteFlag &flag : m_flags) {
    UpdateFlag(ctx, flag, elapsed, opacity, windYaw, paper);
  }

  const float ballOpacity =
      opacity * std::clamp((elapsed - utils::kRouteBallAppear - 0.13f) / 0.19f,
                           0.0f, 1.0f);
  SetTextOpacity(ctx, m_startTitle, ballOpacity);
  SetTextOpacity(ctx, m_startLabel, ballOpacity);
  SetTextOpacity(ctx, m_detail, opacity);

  const float goalProgress = utils::CalculateRouteGoalTitleProgress(elapsed);
  const float lift = (1.0f - EaseOutCubic(goalProgress)) * kGoalTitleLift;
  if (auto *text = ctx.world.Get<components::UIText>(m_goalTitle)) {
    text->y = m_goalTitleY + lift;
  }
  if (auto *text = ctx.world.Get<components::UIText>(m_goalHeading)) {
    text->y = m_goalHeadingY + lift;
  }
  SetTextOpacity(ctx, m_goalTitle, goalProgress * opacity);
  SetTextOpacity(ctx, m_goalHeading, goalProgress * opacity);
}

void GoalRouteEffect::UpdateFlag(core::GameContext &ctx, RouteFlag &flag,
                                 float elapsed, float opacity, float windYaw,
                                 ArticlePaperEffect &paper) {
  const float progress = utils::CalculateRouteFlagProgress(elapsed, flag.route);
  const float rise = utils::CalculateFlagRise(progress);
  const float unfurl = utils::CalculateFlagUnfurl(progress);
  const float unit = flag.metersPerFlagMeter;

  for (const FlagPart &part : flag.parts) {
    auto *transform = ctx.world.Get<components::Transform>(part.entity);
    auto *renderer = ctx.world.Get<components::MeshRenderer>(part.entity);
    if (!transform || !renderer) {
      continue;
    }
    // 竿はカップから伸びるので、高さ方向だけを rise 倍する
    paper.SetAttachedLocal(part.entity,
                           {flag.anchor.x + part.offset.x * unit,
                            flag.anchor.y + part.offset.y * unit * rise,
                            flag.anchor.z});
    transform->scale = {part.scale.x * unit, part.scale.y * unit,
                        part.scale.z * unit};
    bool visible = rise > 0.0f;
    if (part.role == PartRole::Pole) {
      transform->scale.y *= rise;
    } else if (part.role == PartRole::Cloth) {
      // 布は竿側から広がる。広がるにつれてなびきも強くする
      transform->scale.x *= std::max(unfurl, 0.001f);
      visible = unfurl > 0.0f;
      if (auto *holeFlag = ctx.world.Get<components::HoleFlag>(part.entity)) {
        holeFlag->amplitude = flag.clothAmplitude * unfurl;
        holeFlag->yawOffset = kClothViewYaw - windYaw;
      }
    } else if (part.role == PartRole::Seam) {
      visible = unfurl > 0.0f;
    }
    renderer->isVisible = visible && opacity > 0.0f;
  }

  // カップは線が届くと開く
  if (auto *text = ctx.world.Get<components::UIText>(flag.cup)) {
    const float open = EaseOutCubic(progress / 0.3f);
    const float width = flag.cupWidth * open;
    const float height = width * 0.32f;
    text->x = flag.route.x - width * 0.5f;
    text->y = utils::kRouteCupY - height * 0.5f;
    text->width = width;
    text->height = height;
    text->style.cornerRadius = height * 0.5f;
    text->visible = progress > 0.0f && opacity > 0.001f;
    text->opacity = opacity;
  }
  const float labelOpacity = utils::CalculateRouteLabelOpacity(progress) * opacity;
  SetTextOpacity(ctx, flag.stepLabel, labelOpacity);
  SetTextOpacity(ctx, flag.remainingLabel, labelOpacity);
}

void GoalRouteEffect::SetTextOpacity(core::GameContext &ctx, ecs::Entity entity,
                                     float opacity) {
  if (auto *text = ctx.world.Get<components::UIText>(entity)) {
    text->opacity = opacity;
    text->visible = opacity > 0.001f;
  }
}

ecs::Entity GoalRouteEffect::AddText(core::GameContext &ctx, float x, float y,
                                     float width, float height, int layer) {
  const ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &text = ctx.world.Add<components::UIText>(entity);
  text.x = x;
  text.y = y;
  text.width = width;
  text.height = height;
  text.visible = false;
  text.opacity = 0.0f;
  text.layer = layer;
  return entity;
}

void GoalRouteEffect::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_flags.clear();
  m_lineSegments.clear();
  m_lineSegmentStarts.clear();
  m_ball = UINT32_MAX;
  m_detail = UINT32_MAX;
  m_startTitle = UINT32_MAX;
  m_startLabel = UINT32_MAX;
  m_goalHeading = UINT32_MAX;
  m_goalTitle = UINT32_MAX;
  m_active = false;
}

} // namespace game::controllers
