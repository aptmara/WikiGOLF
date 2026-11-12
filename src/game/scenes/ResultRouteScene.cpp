#include "ResultRouteScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../graphics/TextRenderer.h"
#include "../systems/WikiClient.h"
#include <algorithm>
#include <cmath>
#include <shellapi.h>

namespace game::scenes {
namespace {
constexpr float kLineHeight = 32.0f;
constexpr float kArticleWidth = 280.0f;
constexpr float kColumnStep = 320.0f;
constexpr float kViewportHeight = 128.0f;
constexpr float kTrackX = 1120.0f;
constexpr float kTrackWidth = 12.0f;
const D2D1_RECT_F kClose = {1000, 74, 1148, 118};

graphics::TextStyle TextStyle(float size = 22.0f) {
  auto style = graphics::TextStyle::Default();
  style.fontFamily = "Kiwi Maru Medium";
  style.fontSize = size;
  style.color = {0.94f, 0.96f, 1.0f, 1.0f};
  style.align = graphics::TextAlign::Left;
  style.hasShadow = false;
  style.hasOutline = false;
  style.bgColor = {0, 0, 0, 0};
  return style;
}

float CardTop(int index) { return 188.0f + index * 206.0f; }
float ViewTop(int index) { return CardTop(index) + 48.0f; }

bool Contains(const D2D1_RECT_F &rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

D2D1_RECT_F VisibleRect(float x, float y, float height, int index) {
  return {x, std::max(y, ViewTop(index)), x + kArticleWidth,
          std::min(y + height, ViewTop(index) + kViewportHeight)};
}

D2D1_RECT_F Thumb(int index, float height, float scroll) {
  const float thumbHeight = std::max(24.0f,
      kViewportHeight * kViewportHeight / height);
  const float y = ViewTop(index) + (kViewportHeight - thumbHeight) *
      scroll / (height - kViewportHeight);
  return {kTrackX, y, kTrackX + kTrackWidth, y + thumbHeight};
}

// UTF-16 のサロゲートペアを分断せず、実際のフォント計測で改行する。
std::vector<std::wstring> WrapTitle(graphics::TextRenderer &renderer,
                                   const std::wstring &title) {
  const auto style = TextStyle();
  const float singleHeight = renderer.MeasureTextHeight(L"国", style, 256);
  std::vector<std::wstring> lines;
  std::wstring line;
  for (std::size_t i = 0; i < title.size();) {
    std::size_t length = 1;
    if (title[i] >= 0xd800 && title[i] <= 0xdbff && i + 1 < title.size() &&
        title[i + 1] >= 0xdc00 && title[i + 1] <= 0xdfff) {
      length = 2;
    }
    const auto next = title.substr(i, length);
    if (!line.empty() &&
        renderer.MeasureTextHeight(line + next, style, 256) > singleHeight + 1) {
      lines.push_back(line);
      line.clear();
    }
    line += next;
    i += length;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}
} // namespace

void ResultRouteScene::BuildRoute(
    core::GameContext &ctx, Route &route,
    const std::vector<game::utils::ArticleVisit> &visits) {
  route = {};
  if (!ctx.textRenderer || !ctx.textRenderer->IsValid()) return;
  float rowTop = 0;
  float rowHeight = 0;
  for (std::size_t i = 0; i < visits.size(); ++i) {
    if (i > 0 && i % 3 == 0) {
      rowTop += rowHeight + 16;
      rowHeight = 0;
    }
    Article article;
    article.title = visits[i].title;
    article.returned = visits[i].returned;
    article.lines = WrapTitle(*ctx.textRenderer, core::ToWString(article.title));
    article.x = 156 + static_cast<float>(i % 3) * kColumnStep;
    article.y = rowTop;
    article.height = 16 + static_cast<float>(article.lines.size()) * kLineHeight;
    rowHeight = std::max(rowHeight, article.height);
    route.articles.push_back(std::move(article));
  }
  route.height = rowTop + rowHeight;
}

void ResultRouteScene::OnEnter(core::GameContext &ctx) {
  BuildRoute(ctx, m_routes[0], m_data.visitedRoute);
  ConsumeSearch(ctx);
}

void ResultRouteScene::ConsumeSearch(core::GameContext &ctx) {
  if (m_searchConsumed || !m_data.shortestRoute ||
      !m_data.shortestRoute->completed.load(std::memory_order_acquire)) return;
  m_searchConsumed = true;
  const auto &result = m_data.shortestRoute->result;
  m_searchSucceeded = result.success && !result.path.empty();
  if (m_searchSucceeded) {
    std::vector<game::utils::ArticleVisit> visits;
    for (const auto &title : result.path) visits.push_back({title, false});
    BuildRoute(ctx, m_routes[1], visits);
  }
}

void ResultRouteScene::OnUpdate(core::GameContext &ctx) {
  ConsumeSearch(ctx);
  const auto mouse = ctx.input.GetMousePosition();
  const float mx = static_cast<float>(mouse.x);
  const float my = static_cast<float>(mouse.y);
  const bool clicked = ctx.input.GetMouseButtonDown(0);
  if (ctx.input.GetKeyDown(VK_ESCAPE) || (clicked && Contains(kClose, mx, my))) {
    ctx.sceneManager->PopScene();
    return;
  }
  if (!ctx.input.GetMouseButton(0)) m_draggingRoute = -1;
  for (int index = 0; index < 2; ++index) {
    auto &route = m_routes[index];
    const float maxScroll = std::max(0.0f, route.height - kViewportHeight);
    const D2D1_RECT_F viewport = {148, ViewTop(index), 1136,
                                ViewTop(index) + kViewportHeight};
    if (maxScroll > 0) {
      if (Contains(viewport, mx, my)) {
        route.scroll = std::clamp(route.scroll -
            ctx.input.GetMouseScrollDelta() * kLineHeight, 0.0f, maxScroll);
      }
      const auto thumb = Thumb(index, route.height, route.scroll);
      if (clicked && Contains(thumb, mx, my)) {
        m_draggingRoute = index;
        m_dragOffset = my - thumb.top;
      }
      if (m_draggingRoute == index) {
        const float travel = kViewportHeight - (thumb.bottom - thumb.top);
        route.scroll = std::clamp((my - m_dragOffset - ViewTop(index)) /
                                     travel * maxScroll, 0.0f, maxScroll);
      }
    }
    if (!clicked || m_draggingRoute >= 0) continue;
    for (const auto &article : route.articles) {
      const auto rect = VisibleRect(article.x,
          ViewTop(index) + article.y - route.scroll, article.height, index);
      if (rect.bottom > rect.top && Contains(rect, mx, my)) {
        const std::string url = "https://ja.wikipedia.org/wiki/" +
            game::systems::WikiClient::UrlEncode(article.title);
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOW);
        return;
      }
    }
  }
}

void ResultRouteScene::Render(core::GameContext &ctx) {
  if (!ctx.textRenderer || !ctx.textRenderer->IsValid()) return;
  auto &renderer = *ctx.textRenderer;
  const auto mouse = ctx.input.GetMousePosition();
  const float mx = static_cast<float>(mouse.x);
  const float my = static_cast<float>(mouse.y);
  renderer.BeginDraw();
  renderer.FillFullScreenRect({0, 0, 0, 0.7f});
  renderer.FillRoundedRect({100, 50, 1180, 670}, 18,
                           {0.025f, 0.06f, 0.12f, 1});
  renderer.DrawRoundedRect({100, 50, 1180, 670}, 18,
                           {0.8f, 0.68f, 0.28f, 1}, 2);
  renderer.RenderText(L"経路の答え合わせ", {132, 76, 960, 120}, TextStyle(30));
  auto closeColor = DirectX::XMFLOAT4{0.12f, 0.19f, 0.30f, 1};
  if (Contains(kClose, mx, my)) closeColor = {0.28f, 0.38f, 0.52f, 1};
  renderer.FillRoundedRect(kClose, 6, closeColor);
  renderer.RenderText(L"閉じる Esc", {1012, 82, 1140, 114}, TextStyle(20));
  std::wstring summary = L"あなた: " +
      std::to_wstring(m_data.visitedRoute.size()) + L"記事 / 最短: ";
  if (m_searchSucceeded) {
    summary += std::to_wstring(m_routes[1].articles.size()) + L"記事";
  } else if (m_data.shortestRoute && !m_searchConsumed) {
    summary += L"確認中";
  } else {
    summary += L"取得できません";
  }
  renderer.RenderText(summary, {132, 130, 1148, 166}, TextStyle(24));
  for (int index = 0; index < 2; ++index) {
    const auto &route = m_routes[index];
    const float top = CardTop(index);
    renderer.FillRoundedRect({132, top, 1148, top + 190}, 10,
                             {0.055f, 0.095f, 0.16f, 1});
    auto headingStyle = TextStyle();
    headingStyle.color = {0.65f, 0.82f, 1, 1};
    std::wstring heading = L"あなたの経路";
    if (index == 1) {
      heading = L"最短経路";
      headingStyle.color = {1, 0.9f, 0.55f, 1};
    }
    renderer.RenderText(heading, {156, top + 10, 500, top + 42}, headingStyle);
    if (index == 1) {
      renderer.RenderText(L"ゲーム収録データに基づく最短経路",
          {600, top + 14, 1120, top + 40}, TextStyle(16));
    }
    if (route.articles.empty()) {
      std::wstring message = L"経路の記録がありません";
      if (index == 1) {
        message = L"最短経路を取得できませんでした";
        if (m_data.shortestRoute && !m_searchConsumed)
          message = L"最短経路を確認しています…";
      }
      renderer.RenderText(message, {156, top + 72, 1100, top + 112}, TextStyle());
    }
    for (std::size_t i = 0; i < route.articles.size(); ++i) {
      const auto &article = route.articles[i];
      const float y = ViewTop(index) + article.y - route.scroll;
      const auto rect = VisibleRect(article.x, y, article.height, index);
      if (rect.bottom <= rect.top) continue;
      const bool hover = Contains(rect, mx, my);
      auto color = DirectX::XMFLOAT4{0.10f, 0.17f, 0.27f, 1};
      if (hover) color = {0.21f, 0.31f, 0.44f, 1};
      renderer.FillRect(rect, color);
      for (std::size_t line = 0; line < article.lines.size(); ++line) {
        const float lineY = y + 8 + static_cast<float>(line) * kLineHeight;
        // 行単位で表示範囲を制限し、カード外への文字描画を防ぐ。
        if (lineY < ViewTop(index) ||
            lineY + kLineHeight > ViewTop(index) + kViewportHeight) continue;
        renderer.RenderText(article.lines[line],
            {article.x + 12, lineY, article.x + 268, lineY + kLineHeight},
            TextStyle());
        if (hover) renderer.FillRect(
            {article.x + 12, lineY + 29, article.x + 268, lineY + 30},
            headingStyle.color);
      }
      if (i > 0 && y >= ViewTop(index) &&
          y + 32 <= ViewTop(index) + kViewportHeight) {
        std::wstring arrow = L"→";
        float arrowX = article.x - 34;
        if (i % 3 == 0) {
          arrow = L"↳";
          arrowX = 134;
        }
        if (article.returned) arrow = L"戻る";
        renderer.RenderText(arrow, {arrowX, y + 8, article.x, y + 32},
                            TextStyle(14));
      }
    }
    if (route.height > kViewportHeight) {
      renderer.FillRoundedRect(
          {kTrackX, ViewTop(index), kTrackX + kTrackWidth,
           ViewTop(index) + kViewportHeight}, 5, {0.15f, 0.21f, 0.3f, 1});
      renderer.FillRoundedRect(Thumb(index, route.height, route.scroll), 5,
                               headingStyle.color);
    }
  }
  renderer.RenderText(L"記事名をクリックするとWikipediaを開きます",
                     {132, 608, 1148, 638}, TextStyle(18));
  renderer.EndDraw();
}

} // namespace game::scenes
