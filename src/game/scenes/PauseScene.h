#pragma once
/**
 * @file PauseScene.h
 * @brief ポーズシーン（ゲーム上にオーバーレイ）
 */

#include "../../core/Scene.h"
#include "../../core/GameContext.h"
#include "../components/UIText.h"
#include "../components/UIButton.h"

namespace game::scenes {

class PauseScene : public core::Scene {
public:
    const char* GetName() const override { return "PauseScene"; }

    void OnEnter(core::GameContext& ctx) override {
        // ポーズタイトル
        auto titleEntity = CreateEntity(ctx.world);
        auto& title = ctx.world.Add<components::UIText>(titleEntity);
        title.text = L"一時停止";
        title.x = 640.0f - 100.0f;
        title.y = 200.0f;
        title.style.fontSize = 48.0f;
        title.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
        title.style.hasShadow = true;
        title.layer = 100;

        // 再開ボタン
        auto resumeEntity = CreateEntity(ctx.world);
        ctx.world.Add<components::UIButton>(resumeEntity) = 
            components::UIButton::Create(L"再開", "resume_game", 540.0f, 320.0f, 200.0f, 60.0f);

        // タイトルに戻るボタン
        auto titleBtnEntity = CreateEntity(ctx.world);
        ctx.world.Add<components::UIButton>(titleBtnEntity) = 
            components::UIButton::Create(L"タイトルへ", "goto_title", 540.0f, 420.0f, 200.0f, 60.0f);
    }
};

} // namespace game::scenes
