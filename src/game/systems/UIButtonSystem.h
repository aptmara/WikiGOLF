#pragma once
/**
 * @file UIButtonSystem.h
 * @brief UIボタンのマウスインタラクション処理
 */

#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../ecs/World.h"
#include "../components/UIButton.h"
#include <functional>

namespace game::systems {

/// @brief ボタンクリックコールバック型
using ButtonClickCallback = std::function<void(const std::string &action)>;

/// @brief UIボタンシステム
class UIButtonSystem {
public:
  /// @brief クリックコールバックを設定
  void SetClickCallback(ButtonClickCallback callback) {
    m_callback = std::move(callback);
  }

  /// @brief システム実行
  void operator()(core::GameContext &ctx) {
    auto mousePos = ctx.input.GetMousePosition();
    float mx = static_cast<float>(mousePos.x);
    float my = static_cast<float>(mousePos.y);
    bool lmbDown = ctx.input.GetMouseButton(0);
    bool lmbPressed = ctx.input.GetMouseButtonDown(0);
    const core::Scene *inputScene =
        ctx.sceneManager ? ctx.sceneManager->Current() : nullptr;
    const bool blocksUnderlyingInput =
        inputScene && inputScene->BlocksUnderlyingInput();

    ctx.world.Query<components::UIButton>().Each(
        [&](ecs::Entity entity, components::UIButton &btn) {
          if (blocksUnderlyingInput && !inputScene->OwnsEntity(entity)) {
            if (btn.state != components::ButtonState::Disabled) {
              btn.state = components::ButtonState::Normal;
            }
            return;
          }
          if (!btn.visible || btn.state == components::ButtonState::Disabled)
            return;

          bool inside = (mx >= btn.x && mx <= btn.x + btn.width &&
                         my >= btn.y && my <= btn.y + btn.height);

          if (inside) {
            if (lmbDown) {
              btn.state = components::ButtonState::Pressed;
            } else {
              btn.state = components::ButtonState::Hovered;
            }

            // クリック検出
            if (lmbPressed && m_callback) {
              m_callback(btn.action);
            }
          } else {
            btn.state = components::ButtonState::Normal;
          }
        });
  }

private:
  ButtonClickCallback m_callback;
};

} // namespace game::systems
