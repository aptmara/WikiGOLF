#pragma once
/**
 * @file SceneManager.h
 * @brief シーン管理（スタック方式）
 */

#include "Logger.h"
#include "Scene.h"
#include <memory>
#include <string>
#include <vector>

namespace core {

/// @brief シーン管理クラス
class SceneManager {
public:
  /// @brief シーンをスタックにプッシュ（現在のシーンの上に追加）
  void PushScene(std::unique_ptr<Scene> scene) {
    m_pendingOp = Op::Push;
    m_pendingScene = std::move(scene);
  }

  /// @brief 現在のシーンをポップ（前のシーンに戻る）
  void PopScene() { m_pendingOp = Op::Pop; }

  /// @brief 現在のシーンを置き換え
  void ChangeScene(std::unique_ptr<Scene> scene) {
    m_pendingOp = Op::Change;
    m_pendingScene = std::move(scene);
  }

  /// @brief 現在のシーンを取得
  Scene *Current() {
    return m_sceneStack.empty() ? nullptr : m_sceneStack.back().get();
  }

  /// @brief フレーム更新（遷移処理とOnUpdate呼び出し）
  void Update(GameContext &ctx) {
    // 遷移リクエストを処理
    ProcessPendingOp(ctx);

    // 現在のシーンを更新
    if (auto *scene = Current()) {
      scene->OnUpdate(ctx);
    }
  }

  /// @brief シーンスタックが空か
  bool IsEmpty() const { return m_sceneStack.empty(); }

private:
  enum class Op { None, Push, Pop, Change };

  void ProcessPendingOp(GameContext &ctx) {
    switch (m_pendingOp) {
    case Op::Push:
      if (m_pendingScene) {
        LOG_INFO("SceneManager", "Push: {}", m_pendingScene->GetName());
        m_pendingScene->OnEnter(ctx);
        m_sceneStack.push_back(std::move(m_pendingScene));
      }
      break;

    case Op::Pop:
      if (!m_sceneStack.empty()) {
        LOG_INFO("SceneManager", "Pop: {}", m_sceneStack.back()->GetName());
        m_sceneStack.back()->OnExit(ctx);
        m_sceneStack.pop_back();
      }
      break;

    case Op::Change:
      if (m_pendingScene) {
        if (!m_sceneStack.empty()) {
          LOG_INFO("SceneManager", "Exit: {}", m_sceneStack.back()->GetName());
          m_sceneStack.back()->OnExit(ctx);
          m_sceneStack.pop_back();
        }
        LOG_INFO("SceneManager", "Change to: {}", m_pendingScene->GetName());
        m_pendingScene->OnEnter(ctx);
        m_sceneStack.push_back(std::move(m_pendingScene));
      }
      break;

    default:
      break;
    }
    m_pendingOp = Op::None;
    m_pendingScene.reset();
  }

  std::vector<std::unique_ptr<Scene>> m_sceneStack;
  Op m_pendingOp = Op::None;
  std::unique_ptr<Scene> m_pendingScene;
};

} // namespace core
