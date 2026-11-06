#pragma once
/**
 * @file GoalNoticePanel.h
 * @brief 記事到着の演出中に右上へ出す、ゲーム中と同じ現在地・ゴールの表示
*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include "../utils/GoalNoticeRules.h"
#include "hud/CourseInfoPanel.h"
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::controllers {

/**
 * @brief 記事到着の演出中、右上にゲーム中と同じ現在地・ゴールの表示を出します。
 * @details ゲーム中のHUDと同じ CourseInfoPanel を使うので、演出が終わってHUDが
 *          戻っても見た目が変わりません。帰属表示もこのパネルに含まれます。
 *          道のりの演出は GoalRouteEffect が担当します。
*/
class GoalNoticePanel {
public:
  /** @brief パネルに表示する内容です。*/
  struct Content {
    bool useVeil = false; ///< 紙を出せないとき、代わりに紙面色で画面を覆います
    std::wstring currentTitle; ///< 到着した記事
    std::wstring goalTitle;
  };

  /** @brief UI Entityを生成します。*/
  void Initialize(core::GameContext &ctx, const Content &content);

  /** @brief 経過時間に合わせて、紙の代わりの幕を薄くします。*/
  void Update(core::GameContext &ctx, float elapsed,
              const utils::ArticleArrivalTiming &timing);

  /** @brief 生成したEntityをすべて破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  hud::CourseInfoPanel m_courseInfo;
  ecs::EntityOwner m_entityOwner;
  ecs::Entity m_veil = UINT32_MAX;
};

} // namespace game::controllers
