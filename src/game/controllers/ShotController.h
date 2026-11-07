#pragma once
/**
 * @file ShotController.h
 * @brief ショット入力の管理およびショット実行処理を行うコントローラー
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "ClubController.h"
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
     * @param club 選択中クラブ(基準飛距離・キャリー距離テーブルを含む)
     * @param timeOfDay 時間経過を反映するための TimeOfDaySystem
     * @param hud 判定結果などを表示するための HUD
*/
    void ExecuteShot(core::GameContext& ctx,
                     ecs::Entity ballEntity,
                     const DirectX::XMFLOAT3& shotDir,
                     const ClubController::Club& club,
                     game::systems::TimeOfDaySystem* timeOfDay,
                     WikiGolfHUD* hud);

#ifdef WIKIGOLF_DEBUG_TOOLS
private:
    /**
     * @brief 計測用の自動ショット。クリックすべき瞬間なら true を返します。
     * @details WIKIGOLF_DEBUG_AUTO_SHOT=秒 のとき、構えられる状態でその秒数待ってから、
     *          パワー 7 割・インパクトはパーフェクト付近で打つ。未指定なら何もしない。
     */
    bool UpdateDebugAutoShot(core::GameContext& ctx, bool canAim);

    bool m_debugAutoShotRead = false;
    float m_debugAutoShotWait = 0.0f;   ///< 0 以下なら自動ショットしない
    float m_debugAutoShotTimer = 0.0f;  ///< 今の段階に入ってからの秒数
    int m_debugAutoShotPhase = -1;      ///< 前フレームの段階（変わったらタイマーを戻す）
#endif
};

} // namespace game::controllers
