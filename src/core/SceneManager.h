#pragma once
/**
 * @file SceneManager.h
 * @brief シーン管理（スタック方式）
 */

#include "Logger.h"
#include "Scene.h"
#include <chrono>
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

  /// @brief フレーム描画
  void Render(GameContext &ctx) {
    // LOG_DEBUG("SceneManager", "Render START");
    if (auto *scene = Current()) {
      // LOG_DEBUG("SceneManager", "Rendering scene: {}", scene->GetName());
      scene->Render(ctx);
    }
    // LOG_DEBUG("SceneManager", "Render FINISHED");
  }

  /// @brief シーンスタックが空か
  bool IsEmpty() const { return m_sceneStack.empty(); }

private:
  enum class Op { None, Push, Pop, Change };

  /// @brief シーン処理の経過時間をミリ秒で返します。 山内陽
  static long long ElapsedMs(
      const std::chrono::steady_clock::time_point &startedAt) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
  }

  void ProcessPendingOp(GameContext &ctx) {
    if (m_pendingOp != Op::None) {
      LOG_INFO("SceneManager", "Total entities before op: {}", ctx.world.GetEntityCount());
    }
    switch (m_pendingOp) {
    case Op::Push:
      if (m_pendingScene) {
        LOG_INFO("SceneManager", "Push: {}", m_pendingScene->GetName());
        const auto enterStartedAt = std::chrono::steady_clock::now();
        m_pendingScene->OnEnter(ctx);
        LOG_INFO("SceneManager", "OnEnter finished: {} ({} ms)",
                 m_pendingScene->GetName(), ElapsedMs(enterStartedAt));
        m_sceneStack.push_back(std::move(m_pendingScene));
      }
      break;

    case Op::Pop:
      if (!m_sceneStack.empty()) {
        LOG_INFO("SceneManager", "Pop: {}", m_sceneStack.back()->GetName());
        const std::string sceneName = m_sceneStack.back()->GetName();
        const auto exitStartedAt = std::chrono::steady_clock::now();
        m_sceneStack.back()->OnExit(ctx);
        LOG_INFO("SceneManager", "OnExit finished: {} ({} ms)", sceneName,
                 ElapsedMs(exitStartedAt));
        m_sceneStack.pop_back();
      }
      break;

    case Op::Change:
      if (m_pendingScene) {
        if (!m_sceneStack.empty()) {
          LOG_INFO("SceneManager", "Exit: {}", m_sceneStack.back()->GetName());
          const std::string sceneName = m_sceneStack.back()->GetName();
          const auto exitStartedAt = std::chrono::steady_clock::now();
          m_sceneStack.back()->OnExit(ctx);
          LOG_INFO("SceneManager", "OnExit finished: {} ({} ms)", sceneName,
                   ElapsedMs(exitStartedAt));
          m_sceneStack.pop_back();
        }
        LOG_INFO("SceneManager", "Change to: {}", m_pendingScene->GetName());
        const auto enterStartedAt = std::chrono::steady_clock::now();
        m_pendingScene->OnEnter(ctx);
        LOG_INFO("SceneManager", "OnEnter finished: {} ({} ms)",
                 m_pendingScene->GetName(), ElapsedMs(enterStartedAt));
        m_sceneStack.push_back(std::move(m_pendingScene));
      }
      break;

    default:
      break;
    }
    if (m_pendingOp != Op::None) {
      LOG_INFO("SceneManager", "Total entities after op: {}", ctx.world.GetEntityCount());
    }
    m_pendingOp = Op::None;
    m_pendingScene.reset();
  }

  std::vector<std::unique_ptr<Scene>> m_sceneStack;
  Op m_pendingOp = Op::None;
  std::unique_ptr<Scene> m_pendingScene;
};

} // namespace core
