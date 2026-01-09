#include "WikiGameSystem.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"


// TODO: Phase 2で共通ユーティリティへ移動推奨
static std::wstring LocalToWString(const std::string &str) {
  if (str.empty())
    return L"";
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0],
                      size_needed);
  return wstrTo;
}

namespace game::systems {

using namespace game::components;

void WikiGameSystem(core::GameContext &ctx) {
  auto *events = ctx.world.GetGlobal<CollisionEvents>();
  if (!events || events->events.empty())
    return;

  auto *gameState = ctx.world.GetGlobal<WikiGameState>();
  if (!gameState)
    return;

  // 衝突イベント処理
  for (const auto &evt : events->events) {
    // Headingとの衝突チェック
    // どちらかがHeadingを持っているか確認
    uint32_t targetEntity = 0;

    if (ctx.world.Has<Heading>(evt.entityA))
      targetEntity = evt.entityA;
    else if (ctx.world.Has<Heading>(evt.entityB))
      targetEntity = evt.entityB;

    if (targetEntity != 0) {
      auto *h = ctx.world.Get<Heading>(targetEntity);
      if (h && !h->isDestroyed) {
        h->isDestroyed = true;
        gameState->score += 50;

        // リンクターゲットの処理
        if (!h->linkTarget.empty()) {
          gameState->pendingLink = h->linkTarget;

          // 情報UI更新
          if (gameState->infoEntity != 0) {
            auto *infoUI = ctx.world.Get<UIText>(gameState->infoEntity);
            if (infoUI) {
              infoUI->text = L"💡 移動可能: 「" +
                             LocalToWString(h->linkTarget) + L"」 ↑で遷移";
            }
          }
        }

        // スコアUI更新
        if (gameState->scoreEntity != 0) {
          auto *scoreUI = ctx.world.Get<UIText>(gameState->scoreEntity);
          if (scoreUI) {
            scoreUI->text = L"Score: " + std::to_wstring(gameState->score) +
                            L"  Lives: " + std::to_wstring(gameState->lives) +
                            L"  Moves: " +
                            std::to_wstring(gameState->moveCount);
          }
        }

        // オブジェクト削除（物理・描画コンポーネントを削除して無効化）
        // Note:
        // Entity自体を削除するとループ内で無効アクセスになる可能性があるため、
        // コンポーネント削除で対応するのが一般的だが、ここでは即座に削除しても
        // イベントリストがIDを保持しているだけなので安全（Entityが生きていれば）。
        // ただし次回フレーム以降のためにコンポーネントを外す。
        ctx.world.Remove<MeshRenderer>(targetEntity);
        ctx.world.Remove<Collider>(targetEntity);
        // RigidBodyはStaticなら物理影響ないが、念のため
        ctx.world.Remove<RigidBody>(targetEntity);

        LOG_INFO("GameLogic", "Heading hit: {}", h->fullText);
      }
    }
  }
}

} // namespace game::systems
