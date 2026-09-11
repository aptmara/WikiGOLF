#pragma once
/**
 * @file AchievementScene.h
 * @brief 実績一覧を表示するモーダルシーン
 * @details RankingSceneと同型のPushScene方式。通信は一切行わず、
 *          ctx.achievements（AchievementManager）が保持するローカル進捗
 *          だけを読んで表示する。
*/

#include "../../core/Scene.h"
#include <string>
#include <vector>

namespace game::scenes {

class AchievementScene : public core::Scene {
public:
  const char *GetName() const override { return "AchievementScene"; }
  bool BlocksUnderlyingInput() const override { return true; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;
  void Render(core::GameContext &ctx) override;

private:
  /** @brief 左右カラムを現在のスクロール位置に応じて再描画します。*/
  void RefreshColumns(core::GameContext &ctx);

  ecs::Entity m_closeButton = 0;
  ecs::Entity m_leftText = 0;
  ecs::Entity m_rightText = 0;
  std::vector<ecs::Entity> m_hiddenUnderlyingButtons;

  /** @brief 実績1件分のフォーマット済みテキスト（左右カラムに事前分配済み）。*/
  std::vector<std::wstring> m_leftLines;
  std::vector<std::wstring> m_rightLines;

  int m_scrollOffset = 0;    ///< 現在表示中の先頭行インデックス
  int m_maxScrollOffset = 0; ///< スクロール可能な最大インデックス
};

} // namespace game::scenes
