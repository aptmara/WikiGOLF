/**
 * @file ModalSceneRender.cpp
 * @brief モーダルシーン共通描画処理の実装
*/

#include "ModalSceneRender.h"
#include "../../core/GameContext.h"
#include "../../core/Scene.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include <algorithm>
#include <vector>

namespace game::scenes {

void RenderModalScene(core::GameContext &ctx, const core::Scene &scene,
                      const DirectX::XMFLOAT4 &dimColor) {
  if (!ctx.textRenderer || !ctx.textRenderer->IsValid()) {
    return;
  }

  std::vector<const components::UIText *> texts;
  ctx.world.Query<components::UIText>().Each(
      [&](ecs::Entity entity, const components::UIText &text) {
        if (scene.OwnsEntity(entity) && text.visible) {
          texts.push_back(&text);
        }
      });
  std::sort(texts.begin(), texts.end(), [](const auto *left, const auto *right) {
    return left->layer < right->layer;
  });

  ctx.textRenderer->BeginDraw();
  ctx.textRenderer->FillFullScreenRect(dimColor);

  for (const auto *text : texts) {
    const float width = text->width > 0.0f
                            ? text->width
                            : ctx.textRenderer->GetWidth() - text->x;
    const float height = text->height > 0.0f
                             ? text->height
                             : ctx.textRenderer->GetHeight() - text->y;
    const D2D1_RECT_F rect = D2D1::RectF(
        text->x, text->y, text->x + width, text->y + height);
    ctx.textRenderer->RenderText(text->text, rect, text->style);
  }

  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity entity, const components::UIButton &button) {
        if (!scene.OwnsEntity(entity) || !button.visible) {
          return;
        }
        const D2D1_RECT_F buttonRect =
            D2D1::RectF(button.x, button.y, button.x + button.width,
                        button.y + button.height);
        ctx.textRenderer->FillRect(buttonRect, button.GetCurrentColor());

        auto style = button.textStyle;
        style.align = graphics::TextAlign::Center;
        const float textHeight = style.fontSize * 1.2f;
        const float verticalOffset = (button.height - textHeight) / 2.0f;
        const D2D1_RECT_F textRect =
            D2D1::RectF(button.x, button.y + verticalOffset,
                        button.x + button.width,
                        button.y + button.height - verticalOffset);
        ctx.textRenderer->RenderText(button.label, textRect, style);
      });

  ctx.textRenderer->EndDraw();
}

} // namespace game::scenes
