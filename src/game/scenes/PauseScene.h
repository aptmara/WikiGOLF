#pragma once
/**
 * @file PauseScene.h
 * @brief ゲームプレイ上に重ねるポーズシーンです。
 */

#include "../../core/GameContext.h"
#include "../../core/Scene.h"
#include <functional>

namespace game::scenes {

/**
 * @brief インゲーム用のポーズメニューです。
 */
class PauseScene : public core::Scene {
public:
  using ActionCallback = std::function<void(core::GameContext&)>;

  PauseScene(bool canReturnToPreviousPage,
             ActionCallback returnToPreviousPage);

  const char* GetName() const override { return "PauseScene"; }

  void OnEnter(core::GameContext& ctx) override;
  void OnUpdate(core::GameContext& ctx) override;

private:
  /// @brief ボタン見出しを生成します。
  void CreateButton(core::GameContext& ctx, const std::wstring& label,
                    const std::string& action, float x, float y, float width,
                    bool enabled);

  bool m_canReturnToPreviousPage = false;
  ActionCallback m_returnToPreviousPage;
};

} // namespace game::scenes
