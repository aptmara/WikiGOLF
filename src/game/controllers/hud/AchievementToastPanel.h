#pragma once
/**
 * @file AchievementToastPanel.h
 * @brief 実績解除時に画面右下へ積み重ねて表示するトースト通知パネル
 * @details シーンに紐づかない独立コントローラ。AchievementManagerからのみ
 *          呼び出される。スタック先頭（最前面）のみが表示タイマーを進め、
 *          消えると次のトーストが繰り上がる＝「上から順に消える」演出になる。
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace graphics {
class TextRenderer;
}

namespace game::controllers::hud {

/** @brief 右下スタック型の実績解除トースト通知。*/
class AchievementToastPanel {
public:
  /** @brief 新規解除トーストをスタック最前面へ追加します。*/
  void PushToast(core::GameContext &ctx, const std::wstring &name,
                 const std::wstring &description);

  /** @brief 毎フレーム呼び出し、位置・明るさ・表示タイマーを更新します。*/
  void Update(core::GameContext &ctx, float dt);

  /** @brief 全UI描画パス・シーンオーバーレイの後に呼び出し、
   *         どの要素よりも手前へ直接描画します。*/
  void Render(core::GameContext &ctx, graphics::TextRenderer &renderer) const;

  /** @brief 生成した全エンティティを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  struct ToastEntry {
    ecs::Entity background = UINT32_MAX;
    ecs::Entity title = UINT32_MAX;
    ecs::Entity description = UINT32_MAX;
    std::wstring name;
    std::wstring descriptionText;
    float age = 0.0f;         ///< 先頭に立ってからの経過時間
    float fadeProgress = 0.0f; ///< 0=不透明, 1=消滅
    bool fadingOut = false;
    float currentY = 0.0f;    ///< 補間中のY座標（スタック移動を滑らかに）
    float currentBrightness = 1.0f; ///< 補間中の明るさ倍率
  };

  void SpawnEntities(core::GameContext &ctx, ToastEntry &entry);
  void ApplyVisualState(core::GameContext &ctx, ToastEntry &entry,
                        float targetY, float targetBrightness, float dt);
  void DestroyEntry(core::GameContext &ctx, ToastEntry &entry);

  std::vector<ToastEntry> m_active;
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers::hud
