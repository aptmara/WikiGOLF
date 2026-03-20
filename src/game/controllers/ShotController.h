#pragma once
/**
 * @file ShotController.h
 * @brief ショット入力の管理およびショット実行処理を行うコントローラー
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include <DirectXMath.h>

namespace game::systems {
class TimeOfDaySystem;
}

namespace game::controllers {

class WikiGolfHUD;

/**
 * @brief ショット処理コントローラー
 */
class ShotController {
public:
    ShotController() = default;
    ~ShotController() = default;

    struct ShotEvent {
        bool showResult = false;
        bool uiClicked = false;
        bool shotFired = false;
    };

    /**
     * @brief ショット入力の受付とゲージ処理
     * @param ctx ゲームコンテキスト
     * @param canAim 狙いをつけることが可能な状態か
     * @return ショット関連のイベントフラグ
     */
    ShotEvent ProcessShot(core::GameContext& ctx, bool canAim, WikiGolfHUD* hud, class ClubController* clubCtrl);

    /**
     * @brief ショットの実行処理
     * @param ctx ゲームコンテキスト
     * @param ballEntity ボールのエンティティ
     * @param shotDir ショットの方向
     * @param clubPower 選択中クラブの最大パワー
     * @param clubAngle 選択中クラブの打ち出し角度(度)
     * @param timeOfDay 時間経過を反映するための TimeOfDaySystem
     * @param hud 判定結果などを表示するための HUD
     */
    void ExecuteShot(core::GameContext& ctx,
                     ecs::Entity ballEntity,
                     const DirectX::XMFLOAT3& shotDir,
                     float clubPower,
                     float clubAngle,
                     game::systems::TimeOfDaySystem* timeOfDay,
                     WikiGolfHUD* hud);
};

} // namespace game::controllers
