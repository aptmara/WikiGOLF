/**
 * @file UIBarGaugeRenderSystem.cpp
 * @brief UIBarGaugeRenderSystem の実装
 * @details 打撃ゲージをヤード定規として描画する。パネルを持たず、紙片と記事タブ、
 *          距離札だけで構成し、インパクトの判定に応じて揺れと祝いの演出を重ねる。
 *          演出はすべて経過時間から計算し、粒子などの状態を保持しない。
*/

#include "UIBarGaugeRenderSystem.h"
#include "../../ecs/World.h"
#include "../../graphics/TextRenderer.h"
#include "../components/WikiComponents.h"
#include "../utils/ShotGaugeRules.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <d2d1_1.h>
#include <string>

namespace game::systems {

namespace {

using DirectX::XMFLOAT4;
using game::components::ShotJudgement;
using game::components::UIBarGauge;
using graphics::TextRenderer;

constexpr float kPi = 3.14159265f;
constexpr float kScreenWidth = 1280.0f;
const XMFLOAT4 kPaper = {0.973f, 0.976f, 0.980f, 1.000f};
const XMFLOAT4 kHalo = {1.000f, 1.000f, 1.000f, 1.000f};
const XMFLOAT4 kFlagOutline = {0.000f, 0.000f, 0.000f, 0.750f};

/** @brief 判定ごとの揺れと祝い演出の量です。*/
struct ResultFeel {
  float shakeAmplitude; ///< 揺れの最大幅(px)
  float shakeDuration;  ///< 揺れの長さ(秒)
  float tickWave;       ///< 目盛りの波の高さ(px)。0なら波を出さない
  float chipPop;        ///< 距離札が弾む拡大率
  int scraps;           ///< 紙吹雪の枚数
  int stars;            ///< 星の個数
};

ResultFeel GetResultFeel(ShotJudgement judgement) {
  switch (judgement) {
  case ShotJudgement::Special:
    return {10.0f, 0.50f, 30.0f, 0.50f, 54, 14};
  case ShotJudgement::Great:
    return {7.0f, 0.34f, 20.0f, 0.34f, 26, 0};
  case ShotJudgement::Nice:
    return {5.0f, 0.24f, 12.0f, 0.20f, 9, 0};
  default:
    return {3.0f, 0.16f, 0.0f, 0.06f, 0, 0};
  }
}

/** @brief パワー確定時の揺れです。横に振らず軽く沈めます。*/
constexpr float kConfirmSinkPx = 3.0f;
constexpr float kConfirmSinkDuration = 0.18f;
constexpr float kShakeFrequency = 70.0f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float EaseOut(float t) {
  const float inv = 1.0f - Clamp01(t);
  return 1.0f - inv * inv * inv;
}

XMFLOAT4 Fade(const XMFLOAT4 &color, float opacity) {
  return {color.x, color.y, color.z, color.w * Clamp01(opacity)};
}

/** @brief 粒子ごとに固定の値を返す疑似乱数です。*/
float Seeded(int index) {
  const float s = std::sin(static_cast<float>(index) * 127.1f + 311.7f) *
                  43758.5453f;
  return s - std::floor(s);
}

D2D1_RECT_F Box(float x, float y, float w, float h) {
  return D2D1::RectF(x, y, x + w, y + h);
}

void DrawLabel(TextRenderer &renderer, const std::wstring &text,
               const D2D1_RECT_F &rect, float fontSize, const XMFLOAT4 &color,
               graphics::TextAlign align = graphics::TextAlign::Center) {
  graphics::TextStyle style;
  style.fontFamily = "Kiwi Maru Medium";
  style.fontSize = fontSize;
  style.color = color;
  style.align = align;
  style.valign = graphics::TextVAlign::Middle;
  renderer.RenderText(text, rect, style);
}

void FillStar(TextRenderer &renderer, float x, float y, float radius,
              float rotation, const XMFLOAT4 &color) {
  std::array<D2D1_POINT_2F, 10> points{};
  for (int i = 0; i < 10; ++i) {
    const float angle =
        rotation + static_cast<float>(i) * kPi / 5.0f - kPi / 2.0f;
    const float r = (i % 2) ? radius * 0.45f : radius;
    points[i] = D2D1::Point2F(x + std::cos(angle) * r, y + std::sin(angle) * r);
  }
  renderer.FillPolygon(points.data(), static_cast<UINT32>(points.size()), color);
}

/** @brief 白い縁取り付きの星です。*/
void FillOutlinedStar(TextRenderer &renderer, float x, float y, float radius,
                      float rotation, const XMFLOAT4 &color, float opacity) {
  FillStar(renderer, x, y, radius + 2.5f, rotation, Fade(kHalo, opacity));
  FillStar(renderer, x, y, radius, rotation, Fade(color, opacity));
}

/** @brief 定規に立てるゴルフの旗です。*/
void DrawFlag(TextRenderer &renderer, float x, float baseY, float height,
              const XMFLOAT4 &color, float opacity) {
  const float top = baseY - height;
  renderer.FillRect(Box(x - 1.5f, top, 3.0f, height), Fade(kFlagOutline, opacity));
  const D2D1_POINT_2F outline[] = {D2D1::Point2F(x - 1.5f, top - 3.0f),
                                   D2D1::Point2F(x + 24.0f, top + 6.0f),
                                   D2D1::Point2F(x - 1.5f, top + 16.0f)};
  renderer.FillPolygon(outline, 3, Fade(kFlagOutline, opacity));
  const D2D1_POINT_2F body[] = {D2D1::Point2F(x, top - 1.0f),
                                D2D1::Point2F(x + 20.0f, top + 6.0f),
                                D2D1::Point2F(x, top + 14.0f)};
  renderer.FillPolygon(body, 3, Fade(color, opacity));
}

/** @brief 描画の間で共有する、揺れと登場を適用した後の定規の位置です。*/
struct RulerFrame {
  float x;
  float y;
  float width;
  float height;
  float opacity;
  float RatioToX(float ratio) const { return x + width * Clamp01(ratio); }
};

void DrawTab(TextRenderer &renderer, const RulerFrame &f, float x, float width,
             const std::wstring &label, bool selected) {
  const auto rect = Box(x, f.y - 24.0f, width, 25.0f);
  renderer.FillRect(rect, Fade(selected ? kPaper : game::ui::kColorRulerTab, f.opacity));
  renderer.DrawRoundedRect(rect, 0.0f, Fade(game::ui::kColorBorder, f.opacity), 1.0f);
  DrawLabel(renderer, label, rect, 13.0f,
            Fade(selected ? game::ui::kColorTextPrimary : game::ui::kColorAccent, f.opacity));
}

void DrawTabs(TextRenderer &renderer, const UIBarGauge &gauge, const RulerFrame &f) {
  constexpr float kClubTabWidth = 120.0f;
  constexpr float kHintTabWidth = 138.0f;
  DrawTab(renderer, f, f.x, kClubTabWidth, gauge.tabLabel, true);
  DrawTab(renderer, f, f.x + f.width - kHintTabWidth, kHintTabWidth,
          gauge.hintLabel, false);
}

/** @brief 紙片と、距離を示す青線を描きます。確定後の青線は淡く残します。*/
void DrawPaper(TextRenderer &renderer, const UIBarGauge &gauge, const RulerFrame &f) {
  const auto rect = Box(f.x, f.y, f.width, f.height);
  renderer.FillRect(rect, Fade(kPaper, f.opacity));
  const float fillWidth = f.width * Clamp01(gauge.value);
  if (fillWidth > 0.0f) {
    const bool confirmed = gauge.mode == game::components::UIBarGaugeMode::Impact;
    renderer.FillRect(Box(f.x, f.y + 1.0f, fillWidth, 13.0f),
                      Fade(game::ui::kColorAccent,
                           f.opacity * (confirmed ? 0.20f : 1.0f)));
  }
  renderer.DrawRoundedRect(rect, 0.0f, Fade(game::ui::kColorBorder, f.opacity), 1.0f);
}

/** @brief ヤード目盛りを描きます。結果表示中は針の位置から波が左右へ走ります。*/
void DrawTicks(TextRenderer &renderer, const UIBarGauge &gauge, const RulerFrame &f) {
  const auto ticks = game::utils::GetRulerTicks(gauge.rulerMaxYards);
  if (ticks.minorYards <= 0) {
    return;
  }
  const ResultFeel feel = GetResultFeel(gauge.judgement);
  const bool waving = gauge.resultActive && feel.tickWave > 0.0f;
  const float waveFront = gauge.resultElapsed * 950.0f;
  const float waveLife = Clamp01(1.0f - gauge.resultElapsed / 1.05f);
  const float needleX = f.RatioToX(gauge.markerValue);
  const XMFLOAT4 waveColor = game::utils::GetJudgementColor(gauge.judgement);

  for (int yards = 0; static_cast<float>(yards) <= gauge.rulerMaxYards;
       yards += ticks.minorYards) {
    const float tx = f.x + f.width * (static_cast<float>(yards) / gauge.rulerMaxYards);
    const bool major = yards % ticks.majorYards == 0;
    const float length = major ? 13.0f : 6.0f;

    float wave = 0.0f;
    if (waving) {
      const float e = (std::abs(tx - needleX) - waveFront) / 42.0f;
      wave = std::exp(-e * e) * waveLife * feel.tickWave;
    }
    const bool lifted = wave > 3.0f;
    const float tickWidth = lifted ? 2.0f : 1.0f;
    const XMFLOAT4 &tickColor =
        lifted ? waveColor
               : (major ? game::ui::kColorTextPrimary : game::ui::kColorRulerMinorTick);
    renderer.FillRect(Box(tx - tickWidth * 0.5f, f.y + 14.0f - wave, tickWidth,
                          length + wave),
                      Fade(tickColor, f.opacity));

    if (major) {
      // 両端の数字が紙からはみ出さないよう、端だけ揃えを内側へ寄せる。
      constexpr float kLabelWidth = 60.0f;
      float labelX = tx - kLabelWidth * 0.5f;
      auto align = graphics::TextAlign::Center;
      if (yards == 0) {
        labelX = f.x + 4.0f;
        align = graphics::TextAlign::Left;
      } else if (tx > f.x + f.width - 24.0f) {
        labelX = f.x + f.width - kLabelWidth - 4.0f;
        align = graphics::TextAlign::Right;
      }
      DrawLabel(renderer, std::to_wstring(yards),
                Box(labelX, f.y + 28.0f, kLabelWidth, 20.0f), 13.0f,
                Fade(game::ui::kColorTextPrimary, f.opacity), align);
    }
  }
}

/** @brief 判定帯を、幅と高さの両方で段階がわかるように描きます。*/
void DrawZones(TextRenderer &renderer, const UIBarGauge &gauge, const RulerFrame &f) {
  if (!gauge.showImpactZones) {
    return;
  }
  const float grow = EaseOut(gauge.zoneGrow);
  const float centerX = f.RatioToX(gauge.impactCenter);
  const float base = f.y + 13.0f;
  const auto drawZone = [&](float zoneWidth, float zoneHeight, const XMFLOAT4 &color) {
    const float half = f.width * zoneWidth * 0.5f * grow;
    const float left = std::clamp(centerX - half, f.x, f.x + f.width);
    const float right = std::clamp(centerX + half, f.x, f.x + f.width);
    if (right <= left) {
      return;
    }
    const float top = base - zoneHeight;
    renderer.FillRect(D2D1::RectF(left - 1.5f, top - 1.5f, right + 1.5f, base + 1.5f),
                      Fade(kHalo, f.opacity));
    renderer.FillRect(D2D1::RectF(left, top, right, base), Fade(color, f.opacity));
  };
  drawZone(gauge.impactWidthNice, 8.0f, game::ui::kColorAccent);
  drawZone(gauge.impactWidthGreat, 18.0f, game::ui::kColorJudgeGreat);
  drawZone(gauge.impactWidthSpecial, 30.0f, game::ui::kColorSpecial);
}

void DrawNeedle(TextRenderer &renderer, const UIBarGauge &gauge, const RulerFrame &f,
                float pulse) {
  const float mx = f.RatioToX(gauge.markerValue);
  const float w = 2.0f + 2.0f * pulse;
  const XMFLOAT4 color = gauge.resultActive
                             ? game::utils::GetJudgementColor(gauge.judgement)
                             : game::ui::kColorTextPrimary;
  renderer.FillRect(Box(mx - w * 0.5f - 1.0f, f.y - 30.0f, w + 2.0f, 44.0f),
                    Fade(kHalo, f.opacity));
  renderer.FillRect(Box(mx - w * 0.5f, f.y - 29.0f, w, 43.0f), Fade(color, f.opacity));

  const D2D1_POINT_2F halo[] = {D2D1::Point2F(mx - 9.5f, f.y - 41.0f),
                                D2D1::Point2F(mx + 9.5f, f.y - 41.0f),
                                D2D1::Point2F(mx, f.y - 25.0f)};
  renderer.FillPolygon(halo, 3, Fade(kHalo, f.opacity));
  const D2D1_POINT_2F head[] = {D2D1::Point2F(mx - 7.0f, f.y - 39.0f),
                                D2D1::Point2F(mx + 7.0f, f.y - 39.0f),
                                D2D1::Point2F(mx, f.y - 28.0f)};
  renderer.FillPolygon(head, 3, Fade(color, f.opacity));
}

/** @brief 祝いの演出です。紙吹雪と星を飛ばし、パーフェクトには光を加えます。*/
void DrawCelebration(TextRenderer &renderer, const UIBarGauge &gauge,
                     const RulerFrame &f) {
  if (!gauge.resultActive) {
    return;
  }
  const ResultFeel feel = GetResultFeel(gauge.judgement);
  const float t = gauge.resultElapsed;
  const float mx = f.RatioToX(gauge.markerValue);
  const XMFLOAT4 tier = game::utils::GetJudgementColor(gauge.judgement);

  if (gauge.judgement == ShotJudgement::Special) {
    const float ring = Clamp01(t / 0.7f);
    if (ring < 1.0f) {
      const float radius = 30.0f + ring * 300.0f;
      renderer.DrawRoundedRect(
          D2D1::RectF(mx - radius, f.y - radius, mx + radius, f.y + radius), radius,
          Fade(game::ui::kColorGlowGold, f.opacity * (1.0f - ring)),
          9.0f * (1.0f - ring) + 1.0f);
    }
    const float sweep = Clamp01(t / 0.8f);
    if (sweep < 1.0f) {
      const float reach = f.width * sweep;
      const float left = std::clamp(mx - reach, f.x, f.x + f.width);
      const float right = std::clamp(mx + reach, f.x, f.x + f.width);
      renderer.FillRect(D2D1::RectF(left, f.y - 3.0f, right, f.y),
                        Fade(game::ui::kColorGlowGold, f.opacity * (1.0f - sweep)));
    }
  }

  constexpr float kGravity = 780.0f;
  for (int i = 0; i < feel.scraps; ++i) {
    const float life = 1.0f + Seeded(i) * 0.7f;
    if (t > life) {
      continue;
    }
    const float angle = -kPi * (0.14f + 0.72f * Seeded(i + 40));
    const float speed = 260.0f + 560.0f * Seeded(i + 80);
    const float px = mx + std::cos(angle) * speed * t +
                     std::sin(t * 9.0f + static_cast<float>(i)) * 10.0f * t;
    const float py = f.y - 12.0f + std::sin(angle) * speed * t + kGravity * t * t;
    const float w = (5.0f + 7.0f * Seeded(i + 120)) * 0.5f;
    // 裏返る動きは高さの伸縮で表す。
    const float flip = std::abs(std::cos(t * (8.0f + 9.0f * Seeded(i + 200)) +
                                         static_cast<float>(i)));
    const float h = (3.0f + 4.0f * Seeded(i + 160)) * 0.5f * flip + 0.5f;
    const float rotation = t * 5.0f * (Seeded(i + 240) - 0.5f) + static_cast<float>(i);
    const float cs = std::cos(rotation);
    const float sn = std::sin(rotation);
    const auto corner = [&](float dx, float dy) {
      return D2D1::Point2F(px + dx * cs - dy * sn, py + dx * sn + dy * cs);
    };
    const D2D1_POINT_2F points[] = {corner(-w, -h), corner(w, -h), corner(w, h),
                                    corner(-w, h)};
    const XMFLOAT4 &color = i % 3 == 0 ? kPaper
                            : i % 3 == 1 ? tier
                                         : game::ui::kColorAccent;
    renderer.FillPolygon(points, 4,
                         Fade(color, f.opacity * Clamp01((life - t) / 0.3f)));
  }

  for (int i = 0; i < feel.stars; ++i) {
    const float life = 0.9f + Seeded(i + 300) * 0.4f;
    if (t > life) {
      continue;
    }
    const float angle = -kPi * (static_cast<float>(i) + 0.5f) /
                        static_cast<float>(feel.stars);
    const float speed = 170.0f + 150.0f * Seeded(i + 340);
    const float progress = 1.0f - Clamp01(t / life);
    const float distance = speed * (1.0f - progress * progress);
    FillOutlinedStar(renderer, mx + std::cos(angle) * distance,
                     f.y - 16.0f + std::sin(angle) * distance,
                     7.0f + 7.0f * Seeded(i + 380),
                     t * 3.0f + static_cast<float>(i), game::ui::kColorStarGold,
                     f.opacity * Clamp01((life - t) / 0.3f));
  }
}

/** @brief 距離札です。判定の文字は出さず、数字と枠で結果を伝えます。*/
void DrawChip(TextRenderer &renderer, const UIBarGauge &gauge, const RulerFrame &f) {
  const bool result = gauge.resultActive;
  const bool gold = result && gauge.judgement == ShotJudgement::Special;
  const std::wstring text = std::to_wstring(gauge.chipYards) + L"y";
  const float baseFont = result ? 30.0f : 22.0f;
  const float baseHeight = result ? 46.0f : 34.0f;
  const float baseWidth = static_cast<float>(text.size()) * baseFont * 0.6f +
                          (result ? 36.0f : 20.0f);
  const float anchorX = f.RatioToX(gauge.chipAnchor);
  const float left = std::clamp(anchorX - baseWidth * 0.5f, 12.0f,
                                kScreenWidth - baseWidth - 12.0f);
  const float bottom = f.y - 46.0f;

  const float elapsed = result ? gauge.resultElapsed : gauge.confirmElapsed;
  const float pop = Clamp01(1.0f - elapsed / 0.3f);
  const float scale =
      1.0f + (result ? GetResultFeel(gauge.judgement).chipPop : 0.12f) * pop;
  const float w = baseWidth * scale;
  const float h = baseHeight * scale;
  const float cx = left + baseWidth * 0.5f;
  const auto rect = Box(cx - w * 0.5f, bottom - h, w, h);

  const XMFLOAT4 tier = result ? game::utils::GetJudgementColor(gauge.judgement)
                               : game::ui::kColorBorder;
  renderer.FillRect(rect, Fade(gold ? game::ui::kColorSpecial : kPaper, f.opacity));
  renderer.DrawRoundedRect(rect, 0.0f, Fade(tier, f.opacity), result ? 2.0f : 1.0f);
  if (result && gauge.judgement != ShotJudgement::Miss) {
    const auto inner = D2D1::RectF(rect.left + 4.5f, rect.top + 4.5f,
                                   rect.right - 4.5f, rect.bottom - 4.5f);
    renderer.DrawRoundedRect(inner, 0.0f,
                             Fade(gold ? kHalo : tier, f.opacity * (gold ? 0.75f : 1.0f)),
                             1.0f);
  }
  const XMFLOAT4 textColor =
      gold ? kHalo : (result ? tier : game::ui::kColorTextPrimary);
  DrawLabel(renderer, text, rect, baseFont * scale, Fade(textColor, f.opacity));

  if (gold) {
    const float spin = gauge.resultElapsed * 1.5f;
    const float midY = (rect.top + rect.bottom) * 0.5f;
    FillOutlinedStar(renderer, rect.left - 20.0f, midY, 12.0f, spin,
                     game::ui::kColorStarGold, f.opacity);
    FillOutlinedStar(renderer, rect.right + 20.0f, midY, 12.0f, -spin,
                     game::ui::kColorStarGold, f.opacity);
  }
}

void DrawRuler(TextRenderer &renderer, const UIBarGauge &gauge) {
  const float entrance = EaseOut(gauge.entrance);

  // 揺れ: インパクトは判定が良いほど強く長く横に振れ、パワー確定は軽く沈むだけにする。
  float offsetX = 0.0f;
  float offsetY = (1.0f - entrance) * 14.0f;
  float needlePulse = 0.0f;
  if (gauge.resultActive) {
    const ResultFeel feel = GetResultFeel(gauge.judgement);
    const float strength = Clamp01(1.0f - gauge.resultElapsed / feel.shakeDuration);
    offsetX = std::sin(gauge.resultElapsed * kShakeFrequency) * feel.shakeAmplitude *
              strength * strength;
    needlePulse = Clamp01(1.0f - gauge.resultElapsed / 0.2f);
  } else {
    const float strength = Clamp01(1.0f - gauge.confirmElapsed / kConfirmSinkDuration);
    offsetY += kConfirmSinkPx * strength * strength;
    needlePulse = Clamp01(1.0f - gauge.confirmElapsed / 0.2f);
  }

  const RulerFrame frame{gauge.x + offsetX, gauge.y + offsetY, gauge.width,
                         gauge.height, Clamp01(gauge.opacity) * entrance};
  if (frame.opacity <= 0.0f) {
    return;
  }

  DrawTabs(renderer, gauge, frame);
  DrawPaper(renderer, gauge, frame);
  DrawTicks(renderer, gauge, frame);
  if (gauge.showAimPinMarker) {
    DrawFlag(renderer, frame.RatioToX(gauge.aimPinValue), frame.y, 36.0f,
             gauge.aimPinMarkerColor, frame.opacity);
  }
  DrawZones(renderer, gauge, frame);
  DrawNeedle(renderer, gauge, frame, needlePulse);
  DrawCelebration(renderer, gauge, frame);
  DrawChip(renderer, gauge, frame);
}

} // namespace

void UIBarGaugeRenderSystem::operator()(core::GameContext &ctx) {
  if (!ctx.textRenderer) {
    return;
  }

  ctx.textRenderer->BeginDraw();

  ctx.world.Query<UIBarGauge>().Each([&](ecs::Entity, UIBarGauge &gauge) {
    if (gauge.isVisible) {
      DrawRuler(*ctx.textRenderer, gauge);
    }
  });

  ctx.textRenderer->EndDraw();
}

} // namespace game::systems
