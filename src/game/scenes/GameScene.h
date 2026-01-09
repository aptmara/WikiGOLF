#pragma once
/**
 * @file GameScene.h
 * @brief ゲームプレイシーン（キューブデモ）
 */

#include "../../core/Scene.h"
#include "../../core/GameContext.h"
#include "../components/Transform.h"
#include "../components/MeshRenderer.h"
#include "../components/Camera.h"
#include "../components/UIText.h"
#include <DirectXMath.h>

namespace game::scenes {

class GameScene : public core::Scene {
public:
    const char* GetName() const override { return "GameScene"; }

    void OnEnter(core::GameContext& ctx) override {
        using namespace DirectX;

        // カメラ
        auto cam = CreateEntity(ctx.world);
        auto& t = ctx.world.Add<components::Transform>(cam);
        t.position = {0.0f, 0.0f, -5.0f};
        ctx.world.Add<components::Camera>(cam);

        // キューブグリッド
        int gridSize = 5;
        float spacing = 2.5f;
        for (int x = -gridSize; x <= gridSize; ++x) {
            for (int z = -gridSize; z <= gridSize; ++z) {
                auto cube = CreateEntity(ctx.world);
                auto& ct = ctx.world.Add<components::Transform>(cube);
                ct.position = {x * spacing, 0.0f, z * spacing + 10.0f};

                auto& renderer = ctx.world.Add<components::MeshRenderer>(cube);
                renderer.mesh = ctx.resource.LoadMesh("builtin/cube");
                renderer.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl", L"shaders/BasicPS.hlsl");
            }
        }

        // FPS表示
        m_fpsEntity = CreateEntity(ctx.world);
        ctx.world.Add<components::UIText>(m_fpsEntity) = components::UIText::FPS();

        // 操作説明
        auto helpEntity = CreateEntity(ctx.world);
        auto& help = ctx.world.Add<components::UIText>(helpEntity);
        help.text = L"ESC: ポーズ";
        help.x = 10.0f;
        help.y = 50.0f;
        help.style.fontSize = 18.0f;
        help.style.color = {0.7f, 0.7f, 0.7f, 1.0f};
    }

    void OnUpdate(core::GameContext& ctx) override {
        // FPS更新
        m_frameCount++;
        if (m_frameCount >= 60) {
            if (auto* ui = ctx.world.Get<components::UIText>(m_fpsEntity)) {
                ui->text = std::format(L"FPS: ~{}", m_frameCount);
            }
            m_frameCount = 0;
        }
    }

    ecs::Entity GetFpsEntity() const { return m_fpsEntity; }

private:
    ecs::Entity m_fpsEntity;
    int m_frameCount = 0;
};

} // namespace game::scenes
