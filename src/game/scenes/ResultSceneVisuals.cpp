/**
 * @file ResultSceneVisuals.cpp
 * @brief ResultSceneの責務別実装です。
 */

#define NOMINMAX
#include "ResultScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/TextRenderer.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "TitleScene.h"
#include "WikiGolfScene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace game::scenes {

using namespace game::components;
using namespace DirectX;

void ResultScene::UpdateVisuals(core::GameContext &ctx) {
  // 地球儀を自転させながら上下に揺らします。
  if (ctx.world.IsAlive(m_globeEntity)) {
    auto *t = ctx.world.Get<Transform>(m_globeEntity);
    if (t) {
      t->position.y = 2.5f + std::sin(m_time * 0.8f) * 0.2f;
      XMVECTOR q = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(23.5f),
                                                    m_time * 0.5f, 0.0f);
      XMStoreFloat4(&t->rotation, q);
    }
  }

  // 装飾リングの呼吸アニメーションと回転を更新します。
  for (size_t i = 0; i < m_rings.size(); ++i) {
    auto &ring = m_rings[i];
    if (!ctx.world.IsAlive(ring.entity))
      continue;
    auto *t = ctx.world.Get<Transform>(ring.entity);
    if (!t)
      continue;

    float currentAngle = ring.phase + m_time * ring.rotationSpeed;

    // リングの回転姿勢を設定します。
    float rotationDirection = -1.0f;
    if (i % 2 == 0) {
      rotationDirection = 1.0f;
    }
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        m_time * 0.2f * rotationDirection, currentAngle, m_time * 0.1f);
    XMStoreFloat4(&t->rotation, q);

    // スケールの拡大縮小を行います。
    float scaleBase = 1.0f + 0.1f * std::sin(m_time + (float)i);
    t->scale = {scaleBase, 1.0f, scaleBase};
  }

  // --- Firework ECS Integration ---
  m_volleyTimer -= ctx.dt;
  if (m_volleyTimer <= 0.0f) {
      m_volleyTimer = m_volleyInterval + (static_cast<float>(rand() % 200) / 100.0f - 1.0f);
      LaunchVolley();
  }

  std::vector<HanabiSpark> newTrails;

  // Update Shells
  for (auto it = m_shells.begin(); it != m_shells.end(); ) {
      auto& sh = *it;
      sh.age += ctx.dt;
      
      if (sh.phase == HanabiShell::Phase::Ascending) {
          sh.vel.y -= 9.8f * ctx.dt;
          sh.vel.x *= 1.0f - (0.5f * ctx.dt);
          sh.vel.z *= 1.0f - (0.5f * ctx.dt);
          sh.pos.x += sh.vel.x * ctx.dt;
          sh.pos.y += sh.vel.y * ctx.dt;
          sh.pos.z += sh.vel.z * ctx.dt;
          
          // Spawn shell ascending trail
          if (rand() % 2 == 0) {
              HanabiSpark trail;
              trail.pos = sh.pos;
              trail.vel = {
                  (static_cast<float>(rand() % 20) / 10.0f - 1.0f) * 0.5f,
                  -2.0f,
                  (static_cast<float>(rand() % 20) / 10.0f - 1.0f) * 0.5f
              };
              trail.color = { 2.5f, 1.8f, 0.5f, 1.0f }; // Glowing gold
              trail.age = 0.0f;
              trail.lifeTime = 0.4f + (static_cast<float>(rand() % 20) / 100.0f);
              trail.size = 0.15f + (static_cast<float>(rand() % 10) / 100.0f);
              trail.drag = 0.95f;
              
              trail.entity = CreateEntity(ctx.world);
              auto& t = ctx.world.Add<Transform>(trail.entity);
              t.position = trail.pos;
              t.scale = { trail.size, trail.size, trail.size };
              
              auto& mr = ctx.world.Add<MeshRenderer>(trail.entity);
              mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
              mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
              mr.color = trail.color;
              mr.isTransparent = true;
              mr.blendMode = components::BlendMode::Add;
              mr.isVisible = true;

              newTrails.push_back(trail);
          }

          if (sh.vel.y < 0.0f) {
              sh.phase = HanabiShell::Phase::FlashFrame;
              sh.age = 0.0f;
              sh.flashRadius = 1.0f;
              // Camera shake on burst!
              m_cameraShake = 1.0f;

              // Spawn ECS sparks (Organic Flash Core)
              int coreSparks = 40 + (rand() % 20);
              for (int i = 0; i < coreSparks; ++i) {
                  float theta = (static_cast<float>(rand() % 628) / 100.0f);
                  float phi = acosf((static_cast<float>(rand() % 200) / 100.0f) - 1.0f);
                  float spd = 2.0f + (static_cast<float>(rand() % 130) / 10.0f); // 2~15
                  
                  HanabiSpark sp;
                  sp.pos = sh.pos;
                  sp.vel = {
                      sinf(phi) * cosf(theta) * spd,
                      sinf(phi) * sinf(theta) * spd,
                      cosf(phi) * spd
                  };
                  sp.color = { 3.0f, 3.0f, 2.5f, 1.0f }; // HDR Bloom overdrive
                  sp.age = 0.0f;
                  sp.lifeTime = 0.15f + (static_cast<float>(rand() % 10) / 100.0f);
                  sp.size = 0.4f + (static_cast<float>(rand() % 30) / 100.0f);
                  sp.drag = 0.85f;
                  
                  // ECS Entity
                  sp.entity = CreateEntity(ctx.world);
                  auto& t = ctx.world.Add<Transform>(sp.entity);
                  t.position = sp.pos;
                  t.scale = { sp.size, sp.size, sp.size };
                  
                  auto& mr = ctx.world.Add<MeshRenderer>(sp.entity);
                  mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
                  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
                  mr.color = sp.color;
                  mr.isTransparent = true;
                  mr.blendMode = components::BlendMode::Add;
                  mr.isVisible = true;

                  m_sparks.push_back(sp);
              }
              
              // Outer Sparks
              int outerSparks = 180 + (rand() % 80);
              DirectX::XMFLOAT4 shellColor;
              int type = rand() % 5;
              if(type == 0) shellColor = {3.0f, 0.5f, 0.5f, 1.0f};
              else if(type == 1) shellColor = {0.5f, 3.0f, 0.5f, 1.0f};
              else if(type == 2) shellColor = {0.5f, 0.5f, 3.0f, 1.0f};
              else if(type == 3) shellColor = {3.0f, 2.0f, 0.5f, 1.0f};
              else shellColor = {0.5f, 3.0f, 3.0f, 1.0f};

              for (int i = 0; i < outerSparks; ++i) {
                  float theta = (static_cast<float>(rand() % 628) / 100.0f);
                  float phi = acosf((static_cast<float>(rand() % 200) / 100.0f) - 1.0f);
                  float spd = 4.0f + (static_cast<float>(rand() % 200) / 10.0f); // 4~24
                  
                  HanabiSpark sp;
                  sp.pos = sh.pos;
                  sp.vel = {
                      sinf(phi) * cosf(theta) * spd,
                      sinf(phi) * sinf(theta) * spd,
                      cosf(phi) * spd
                  };
                  sp.color = shellColor;
                  sp.age = 0.0f;
                  sp.lifeTime = 1.5f + (static_cast<float>(rand() % 100) / 100.0f);
                  sp.size = 0.12f + (static_cast<float>(rand() % 12) / 100.0f);
                  sp.drag = 0.96f;
                  
                  // ECS Entity
                  sp.entity = CreateEntity(ctx.world);
                  auto& t = ctx.world.Add<Transform>(sp.entity);
                  t.position = sp.pos;
                  t.scale = { sp.size, sp.size, sp.size };
                  
                  auto& mr = ctx.world.Add<MeshRenderer>(sp.entity);
                  mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
                  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
                  mr.color = sp.color;
                  mr.isTransparent = true;
                  mr.blendMode = components::BlendMode::Add;
                  mr.isVisible = true;

                  m_sparks.push_back(sp);
              }
          }
          ++it;
      } else if (sh.phase == HanabiShell::Phase::FlashFrame) {
          sh.phase = HanabiShell::Phase::Burst;
          ++it;
      } else if (sh.phase == HanabiShell::Phase::Burst) {
          sh.phase = HanabiShell::Phase::Fading;
          sh.age = 0.0f;
          sh.lifeTime = 3.0f; // Track when to remove shell object
          ++it;
      } else if (sh.phase == HanabiShell::Phase::Fading) {
          if (sh.age >= sh.lifeTime) {
              it = m_shells.erase(it);
          } else {
              ++it;
          }
      }
  }

  // Update Sparks & spawn active trails
  for (auto it = m_sparks.begin(); it != m_sparks.end(); ) {
      auto& sp = *it;
      sp.age += ctx.dt;
      
      if (sp.age >= sp.lifeTime || !ctx.world.IsAlive(sp.entity)) {
          if(ctx.world.IsAlive(sp.entity)) ctx.world.DestroyEntity(sp.entity);
          it = m_sparks.erase(it);
          continue;
      }
      
      // Spawn trail particle behind the spark if it's moving fast
      if (sp.drag < 0.99f && sp.age < sp.lifeTime * 0.7f && (rand() % 4 == 0)) {
          HanabiSpark trail;
          trail.pos = sp.pos;
          trail.vel = {
              sp.vel.x * 0.1f,
              sp.vel.y * 0.1f,
              sp.vel.z * 0.1f
          };
          trail.color = sp.color;
          trail.color.w = sp.color.w * 0.6f;
          trail.age = 0.0f;
          trail.lifeTime = 0.15f + (static_cast<float>(rand() % 10) / 100.0f);
          trail.size = sp.size * 0.6f;
          trail.drag = 0.99f; // static trail, slowly fading
          
          trail.entity = CreateEntity(ctx.world);
          auto& t = ctx.world.Add<Transform>(trail.entity);
          t.position = trail.pos;
          t.scale = { trail.size, trail.size, trail.size };
          
          auto& mr = ctx.world.Add<MeshRenderer>(trail.entity);
          mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
          mr.color = trail.color;
          mr.isTransparent = true;
          mr.blendMode = components::BlendMode::Add;
          mr.isVisible = true;

          newTrails.push_back(trail);
      }

      sp.vel.y -= 9.8f * ctx.dt * 0.5f; // reduced gravity for sparks
      sp.vel.x *= (1.0f - (1.0f - sp.drag) * ctx.dt * 60.0f);
      sp.vel.y *= (1.0f - (1.0f - sp.drag) * ctx.dt * 60.0f);
      sp.vel.z *= (1.0f - (1.0f - sp.drag) * ctx.dt * 60.0f);
      
      sp.pos.x += sp.vel.x * ctx.dt;
      sp.pos.y += sp.vel.y * ctx.dt;
      sp.pos.z += sp.vel.z * ctx.dt;

      // Sync with ECS
      auto* t = ctx.world.Get<Transform>(sp.entity);
      if (t) {
          t->position = sp.pos;
      }
      
      auto* mr = ctx.world.Get<MeshRenderer>(sp.entity);
      if (mr) {
          float fade = 1.0f - (sp.age / sp.lifeTime);
          mr->color.w = fade; // Fade alpha
      }
      
      ++it;
  }

  // Insert new trails into m_sparks vector
  if (!newTrails.empty()) {
      m_sparks.insert(m_sparks.end(), newTrails.begin(), newTrails.end());
  }
}

/**
 * @brief 3Dのビジュアル表示環境を生成します。
 */

} // namespace game::scenes
