/**
 * @file ParticleSystem.h
 * @brief 環境パーティクルシステム - 大気効果（塵、雪、蛍等）
 */

#pragma once

#include "../components/EnvironmentState.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <random>
#include <vector>
#include <wrl/client.h>


namespace game::systems {

using namespace DirectX;
using Microsoft::WRL::ComPtr;

/**
 * @brief 単一パーティクル
 */
struct Particle {
  XMFLOAT3 position;
  XMFLOAT3 velocity;
  XMFLOAT4 color;
  float size;
  float life; // 0 = dead, 1 = full life
  float maxLife;
  float rotation;
  float rotationSpeed;
};

/**
 * @brief パーティクルシステム設定
 */
struct ParticleConfig {
  int maxParticles = 500;
  float spawnRadius = 50.0f;             // 発生範囲
  float spawnHeight = 30.0f;             // 発生高さ
  XMFLOAT3 baseVelocity = {0, -1.0f, 0}; // 基本速度
  float velocityVariance = 0.3f;         // 速度ばらつき
  float sizeMin = 0.05f;
  float sizeMax = 0.15f;
  float lifeMin = 3.0f;
  float lifeMax = 8.0f;
  XMFLOAT4 colorStart = {1, 1, 1, 0.8f};
  XMFLOAT4 colorEnd = {1, 1, 1, 0};
  bool fadeInOut = true; // フェードイン・アウト
  bool rotateParticles = false;
  float gravity = 0.0f;       // 重力影響
  float windInfluence = 0.5f; // 風の影響
};

/**
 * @brief プリセットからパーティクル設定を取得
 */
inline ParticleConfig GetParticleConfig(components::ParticlePreset preset) {
  ParticleConfig config;

  switch (preset) {
  case components::ParticlePreset::None:
    config.maxParticles = 0;
    break;

  case components::ParticlePreset::Dust:
    config.maxParticles = 200;
    config.baseVelocity = {0.1f, 0.02f, 0.05f};
    config.velocityVariance = 0.5f;
    config.sizeMin = 0.02f;
    config.sizeMax = 0.08f;
    config.lifeMin = 5.0f;
    config.lifeMax = 12.0f;
    config.colorStart = {0.9f, 0.85f, 0.7f, 0.4f};
    config.colorEnd = {0.9f, 0.85f, 0.7f, 0.0f};
    config.gravity = 0.01f;
    break;

  case components::ParticlePreset::Snow:
    config.maxParticles = 400;
    config.baseVelocity = {0.0f, -0.8f, 0.0f};
    config.velocityVariance = 0.4f;
    config.sizeMin = 0.03f;
    config.sizeMax = 0.1f;
    config.lifeMin = 8.0f;
    config.lifeMax = 15.0f;
    config.colorStart = {1.0f, 1.0f, 1.0f, 0.9f};
    config.colorEnd = {1.0f, 1.0f, 1.0f, 0.0f};
    config.windInfluence = 0.8f;
    break;

  case components::ParticlePreset::Rain:
    config.maxParticles = 500;
    config.baseVelocity = {0.0f, -8.0f, 0.0f};
    config.velocityVariance = 0.2f;
    config.sizeMin = 0.01f;
    config.sizeMax = 0.02f;
    config.lifeMin = 1.0f;
    config.lifeMax = 2.0f;
    config.colorStart = {0.6f, 0.7f, 0.9f, 0.6f};
    config.colorEnd = {0.6f, 0.7f, 0.9f, 0.0f};
    config.fadeInOut = false;
    break;

  case components::ParticlePreset::Leaves:
    config.maxParticles = 100;
    config.baseVelocity = {0.3f, -0.5f, 0.1f};
    config.velocityVariance = 0.6f;
    config.sizeMin = 0.08f;
    config.sizeMax = 0.2f;
    config.lifeMin = 6.0f;
    config.lifeMax = 12.0f;
    config.colorStart = {0.8f, 0.6f, 0.2f, 0.9f};
    config.colorEnd = {0.6f, 0.4f, 0.1f, 0.0f};
    config.rotateParticles = true;
    config.windInfluence = 1.0f;
    break;

  case components::ParticlePreset::Fireflies:
    config.maxParticles = 80;
    config.baseVelocity = {0.0f, 0.1f, 0.0f};
    config.velocityVariance = 0.8f;
    config.sizeMin = 0.03f;
    config.sizeMax = 0.06f;
    config.lifeMin = 4.0f;
    config.lifeMax = 8.0f;
    config.colorStart = {1.0f, 1.0f, 0.3f, 0.0f};
    config.colorEnd = {0.5f, 1.0f, 0.2f, 0.0f};
    config.spawnHeight = 10.0f;
    break;

  case components::ParticlePreset::Embers:
    config.maxParticles = 150;
    config.baseVelocity = {0.0f, 1.5f, 0.0f};
    config.velocityVariance = 0.5f;
    config.sizeMin = 0.02f;
    config.sizeMax = 0.06f;
    config.lifeMin = 2.0f;
    config.lifeMax = 5.0f;
    config.colorStart = {1.0f, 0.6f, 0.1f, 1.0f};
    config.colorEnd = {1.0f, 0.2f, 0.0f, 0.0f};
    config.gravity = -0.2f; // 上に浮く
    break;

  case components::ParticlePreset::Bubbles:
    config.maxParticles = 100;
    config.baseVelocity = {0.0f, 0.5f, 0.0f};
    config.velocityVariance = 0.4f;
    config.sizeMin = 0.05f;
    config.sizeMax = 0.15f;
    config.lifeMin = 4.0f;
    config.lifeMax = 8.0f;
    config.colorStart = {0.8f, 0.9f, 1.0f, 0.5f};
    config.colorEnd = {0.8f, 0.9f, 1.0f, 0.0f};
    config.gravity = -0.1f;
    break;

  case components::ParticlePreset::Stars:
    config.maxParticles = 300;
    config.baseVelocity = {0.0f, 0.0f, 0.0f};
    config.velocityVariance = 0.1f;
    config.sizeMin = 0.02f;
    config.sizeMax = 0.08f;
    config.lifeMin = 10.0f;
    config.lifeMax = 20.0f;
    config.colorStart = {1.0f, 1.0f, 1.0f, 0.0f};
    config.colorEnd = {1.0f, 1.0f, 1.0f, 0.0f};
    config.spawnRadius = 100.0f;
    config.spawnHeight = 80.0f;
    break;

  case components::ParticlePreset::Pollen:
    config.maxParticles = 150;
    config.baseVelocity = {0.05f, 0.02f, 0.03f};
    config.velocityVariance = 0.6f;
    config.sizeMin = 0.01f;
    config.sizeMax = 0.03f;
    config.lifeMin = 8.0f;
    config.lifeMax = 15.0f;
    config.colorStart = {1.0f, 1.0f, 0.7f, 0.5f};
    config.colorEnd = {1.0f, 1.0f, 0.7f, 0.0f};
    config.windInfluence = 0.9f;
    break;

  case components::ParticlePreset::Ash:
    config.maxParticles = 200;
    config.baseVelocity = {0.1f, -0.3f, 0.05f};
    config.velocityVariance = 0.5f;
    config.sizeMin = 0.02f;
    config.sizeMax = 0.06f;
    config.lifeMin = 5.0f;
    config.lifeMax = 10.0f;
    config.colorStart = {0.3f, 0.3f, 0.3f, 0.7f};
    config.colorEnd = {0.2f, 0.2f, 0.2f, 0.0f};
    config.rotateParticles = true;
    break;

  case components::ParticlePreset::Smoke:
    config.maxParticles = 100;
    config.baseVelocity = {0.0f, 0.3f, 0.0f};
    config.velocityVariance = 0.3f;
    config.sizeMin = 0.2f;
    config.sizeMax = 0.5f;
    config.lifeMin = 4.0f;
    config.lifeMax = 8.0f;
    config.colorStart = {0.4f, 0.4f, 0.45f, 0.3f};
    config.colorEnd = {0.5f, 0.5f, 0.55f, 0.0f};
    config.gravity = -0.05f;
    break;

  case components::ParticlePreset::Sparkles:
    config.maxParticles = 120;
    config.baseVelocity = {0.0f, 0.0f, 0.0f};
    config.velocityVariance = 0.3f;
    config.sizeMin = 0.02f;
    config.sizeMax = 0.05f;
    config.lifeMin = 1.0f;
    config.lifeMax = 3.0f;
    config.colorStart = {1.0f, 1.0f, 1.0f, 1.0f};
    config.colorEnd = {0.8f, 0.9f, 1.0f, 0.0f};
    break;

  case components::ParticlePreset::NebulaGas:
    config.maxParticles = 80;
    config.baseVelocity = {0.02f, 0.01f, 0.02f};
    config.velocityVariance = 0.2f;
    config.sizeMin = 0.3f;
    config.sizeMax = 0.8f;
    config.lifeMin = 10.0f;
    config.lifeMax = 20.0f;
    config.colorStart = {0.5f, 0.3f, 0.8f, 0.15f};
    config.colorEnd = {0.3f, 0.5f, 0.9f, 0.0f};
    config.spawnRadius = 80.0f;
    break;

  default:
    config.maxParticles = 0;
    break;
  }

  return config;
}

/**
 * @brief 環境パーティクルシステム
 */
class EnvironmentParticleSystem {
public:
  EnvironmentParticleSystem() : m_rng(std::random_device{}()) {}

  /**
   * @brief パーティクル設定を適用
   */
  void Configure(const ParticleConfig &config) {
    m_config = config;
    m_particles.clear();
    m_particles.reserve(config.maxParticles);
  }

  /**
   * @brief 更新
   * @param dt デルタタイム
   * @param cameraPos カメラ位置
   * @param windDir 風向き
   */
  void Update(float dt, const XMFLOAT3 &cameraPos, const XMFLOAT3 &windDir) {
    if (m_config.maxParticles <= 0)
      return;

    // 新規パーティクル生成
    SpawnParticles(dt, cameraPos);

    // 既存パーティクル更新
    for (auto &p : m_particles) {
      if (p.life <= 0)
        continue;

      // 速度に風の影響を追加
      p.velocity.x += windDir.x * m_config.windInfluence * dt;
      p.velocity.z += windDir.z * m_config.windInfluence * dt;

      // 重力
      p.velocity.y -= m_config.gravity * dt;

      // 位置更新
      p.position.x += p.velocity.x * dt;
      p.position.y += p.velocity.y * dt;
      p.position.z += p.velocity.z * dt;

      // 回転
      p.rotation += p.rotationSpeed * dt;

      // 寿命減少
      p.life -= dt / p.maxLife;

      // 色補間
      float t = 1.0f - p.life;
      if (m_config.fadeInOut) {
        // フェードイン・アウト (0->1->0)
        float alpha = p.life < 0.5f ? p.life * 2.0f : (1.0f - p.life) * 2.0f;
        p.color.w = m_config.colorStart.w * alpha;
      } else {
        p.color.w = m_config.colorStart.w * p.life;
      }

      // 色のグラデーション
      p.color.x = m_config.colorStart.x +
                  (m_config.colorEnd.x - m_config.colorStart.x) * t;
      p.color.y = m_config.colorStart.y +
                  (m_config.colorEnd.y - m_config.colorStart.y) * t;
      p.color.z = m_config.colorStart.z +
                  (m_config.colorEnd.z - m_config.colorStart.z) * t;
    }

    // 死んだパーティクルを削除
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const Particle &p) { return p.life <= 0; }),
        m_particles.end());
  }

  /**
   * @brief パーティクルリスト取得
   */
  const std::vector<Particle> &GetParticles() const { return m_particles; }

  /**
   * @brief アクティブパーティクル数
   */
  size_t GetActiveCount() const { return m_particles.size(); }

private:
  void SpawnParticles(float dt, const XMFLOAT3 &cameraPos) {
    // 毎フレーム一定数を生成（パーティクル数に応じて調整）
    float spawnRate =
        static_cast<float>(m_config.maxParticles) / 5.0f; // 5秒で最大数に
    int toSpawn = static_cast<int>(spawnRate * dt);

    std::uniform_real_distribution<float> distAngle(0, XM_2PI);
    std::uniform_real_distribution<float> distRadius(0, m_config.spawnRadius);
    std::uniform_real_distribution<float> distHeight(0, m_config.spawnHeight);
    std::uniform_real_distribution<float> distVel(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distSize(m_config.sizeMin,
                                                   m_config.sizeMax);
    std::uniform_real_distribution<float> distLife(m_config.lifeMin,
                                                   m_config.lifeMax);
    std::uniform_real_distribution<float> distRot(0, XM_2PI);
    std::uniform_real_distribution<float> distRotSpeed(-1.0f, 1.0f);

    for (int i = 0;
         i < toSpawn &&
         m_particles.size() < static_cast<size_t>(m_config.maxParticles);
         ++i) {
      Particle p;

      // カメラ周辺にスポーン
      float angle = distAngle(m_rng);
      float radius = distRadius(m_rng);
      p.position.x = cameraPos.x + cosf(angle) * radius;
      p.position.y = cameraPos.y + distHeight(m_rng);
      p.position.z = cameraPos.z + sinf(angle) * radius;

      // 速度
      p.velocity.x =
          m_config.baseVelocity.x + distVel(m_rng) * m_config.velocityVariance;
      p.velocity.y =
          m_config.baseVelocity.y + distVel(m_rng) * m_config.velocityVariance;
      p.velocity.z =
          m_config.baseVelocity.z + distVel(m_rng) * m_config.velocityVariance;

      // その他のプロパティ
      p.size = distSize(m_rng);
      p.maxLife = distLife(m_rng);
      p.life = 1.0f;
      p.color = m_config.colorStart;
      p.rotation = distRot(m_rng);
      p.rotationSpeed = m_config.rotateParticles ? distRotSpeed(m_rng) : 0.0f;

      m_particles.push_back(p);
    }
  }

  ParticleConfig m_config;
  std::vector<Particle> m_particles;
  std::mt19937 m_rng;
};

} // namespace game::systems
