/**
 * @file ParticleSystem.h
 * @brief 迺ｰ蠅・ヱ繝ｼ繝・ぅ繧ｯ繝ｫ繧ｷ繧ｹ繝・Β - 螟ｧ豌怜柑譫懶ｼ亥｡ｵ縲・妛縲∬寫遲会ｼ・
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
 * @brief 蜊倅ｸ繝代・繝・ぅ繧ｯ繝ｫ
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
 * @brief 繝代・繝・ぅ繧ｯ繝ｫ繧ｷ繧ｹ繝・Β險ｭ螳・
 */
struct ParticleConfig {
  int maxParticles = 500;
  float spawnRadius = 50.0f;             // 逋ｺ逕溽ｯ・峇
  float spawnHeight = 30.0f;             // 逋ｺ逕滄ｫ倥＆
  XMFLOAT3 baseVelocity = {0, -1.0f, 0}; // 蝓ｺ譛ｬ騾溷ｺｦ
  float velocityVariance = 0.3f;         // 騾溷ｺｦ縺ｰ繧峨▽縺・
  float sizeMin = 0.05f;
  float sizeMax = 0.15f;
  float lifeMin = 3.0f;
  float lifeMax = 8.0f;
  XMFLOAT4 colorStart = {1, 1, 1, 0.8f};
  XMFLOAT4 colorEnd = {1, 1, 1, 0};
  bool fadeInOut = true; // 繝輔ぉ繝ｼ繝峨う繝ｳ繝ｻ繧｢繧ｦ繝・
  bool rotateParticles = false;
  float gravity = 0.0f;       // 驥榊鴨蠖ｱ髻ｿ
  float windInfluence = 0.5f; // 鬚ｨ縺ｮ蠖ｱ髻ｿ
};

/**
 * @brief 繝励Μ繧ｻ繝・ヨ縺九ｉ繝代・繝・ぅ繧ｯ繝ｫ險ｭ螳壹ｒ蜿門ｾ・
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
    config.gravity = -0.2f; // 荳翫↓豬ｮ縺・
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
 * @brief 迺ｰ蠅・ヱ繝ｼ繝・ぅ繧ｯ繝ｫ繧ｷ繧ｹ繝・Β
 */
class EnvironmentParticleSystem {
public:
  EnvironmentParticleSystem() : m_rng(std::random_device{}()) {}

  /**
   * @brief 繝代・繝・ぅ繧ｯ繝ｫ險ｭ螳壹ｒ驕ｩ逕ｨ
   */
  void Configure(const ParticleConfig &config) {
    m_config = config;
    m_particles.clear();
    m_particles.reserve(config.maxParticles);
  }

  /**
   * @brief 譖ｴ譁ｰ
   * @param dt 繝・Ν繧ｿ繧ｿ繧､繝
   * @param cameraPos 繧ｫ繝｡繝ｩ菴咲ｽｮ
   * @param windDir 鬚ｨ蜷代″
   */
  void Update(float dt, const XMFLOAT3 &cameraPos, const XMFLOAT3 &windDir) {
    if (m_config.maxParticles <= 0)
      return;

    // 譁ｰ隕上ヱ繝ｼ繝・ぅ繧ｯ繝ｫ逕滓・
    SpawnParticles(dt, cameraPos);

    // 譌｢蟄倥ヱ繝ｼ繝・ぅ繧ｯ繝ｫ譖ｴ譁ｰ
    for (auto &p : m_particles) {
      if (p.life <= 0)
        continue;

      // 騾溷ｺｦ縺ｫ鬚ｨ縺ｮ蠖ｱ髻ｿ繧定ｿｽ蜉
      p.velocity.x += windDir.x * m_config.windInfluence * dt;
      p.velocity.z += windDir.z * m_config.windInfluence * dt;

      // 驥榊鴨
      p.velocity.y -= m_config.gravity * dt;

      // 菴咲ｽｮ譖ｴ譁ｰ
      p.position.x += p.velocity.x * dt;
      p.position.y += p.velocity.y * dt;
      p.position.z += p.velocity.z * dt;

      // 蝗櫁ｻ｢
      p.rotation += p.rotationSpeed * dt;

      // 蟇ｿ蜻ｽ貂帛ｰ・
      p.life -= dt / p.maxLife;

      // 濶ｲ陬憺俣
      float t = 1.0f - p.life;
      if (m_config.fadeInOut) {
        // 繝輔ぉ繝ｼ繝峨う繝ｳ繝ｻ繧｢繧ｦ繝・(0->1->0)
        float alpha = (1.0f - p.life) * 2.0f;
        if (p.life < 0.5f) {
          alpha = p.life * 2.0f;
        }
        p.color.w = m_config.colorStart.w * alpha;
      } else {
        p.color.w = m_config.colorStart.w * p.life;
      }

      // 濶ｲ縺ｮ繧ｰ繝ｩ繝・・繧ｷ繝ｧ繝ｳ
      p.color.x = m_config.colorStart.x +
                  (m_config.colorEnd.x - m_config.colorStart.x) * t;
      p.color.y = m_config.colorStart.y +
                  (m_config.colorEnd.y - m_config.colorStart.y) * t;
      p.color.z = m_config.colorStart.z +
                  (m_config.colorEnd.z - m_config.colorStart.z) * t;
    }

    // 豁ｻ繧薙□繝代・繝・ぅ繧ｯ繝ｫ繧貞炎髯､
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const Particle &p) { return p.life <= 0; }),
        m_particles.end());
  }

  /**
   * @brief 繝代・繝・ぅ繧ｯ繝ｫ繝ｪ繧ｹ繝亥叙蠕・
   */
  const std::vector<Particle> &GetParticles() const { return m_particles; }

  /**
   * @brief 繧｢繧ｯ繝・ぅ繝悶ヱ繝ｼ繝・ぅ繧ｯ繝ｫ謨ｰ
   */
  size_t GetActiveCount() const { return m_particles.size(); }

private:
  void SpawnParticles(float dt, const XMFLOAT3 &cameraPos) {
    // 豈弱ヵ繝ｬ繝ｼ繝荳螳壽焚繧堤函謌撰ｼ医ヱ繝ｼ繝・ぅ繧ｯ繝ｫ謨ｰ縺ｫ蠢懊§縺ｦ隱ｿ謨ｴ・・
    float spawnRate =
        static_cast<float>(m_config.maxParticles) / 5.0f; // 5遘偵〒譛螟ｧ謨ｰ縺ｫ
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

      // 繧ｫ繝｡繝ｩ蜻ｨ霎ｺ縺ｫ繧ｹ繝昴・繝ｳ
      float angle = distAngle(m_rng);
      float radius = distRadius(m_rng);
      p.position.x = cameraPos.x + cosf(angle) * radius;
      p.position.y = cameraPos.y + distHeight(m_rng);
      p.position.z = cameraPos.z + sinf(angle) * radius;

      // 騾溷ｺｦ
      p.velocity.x =
          m_config.baseVelocity.x + distVel(m_rng) * m_config.velocityVariance;
      p.velocity.y =
          m_config.baseVelocity.y + distVel(m_rng) * m_config.velocityVariance;
      p.velocity.z =
          m_config.baseVelocity.z + distVel(m_rng) * m_config.velocityVariance;

      // 縺昴・莉悶・繝励Ο繝代ユ繧｣
      p.size = distSize(m_rng);
      p.maxLife = distLife(m_rng);
      p.life = 1.0f;
      p.color = m_config.colorStart;
      p.rotation = distRot(m_rng);
      p.rotationSpeed = 0.0f;
      if (m_config.rotateParticles) {
        p.rotationSpeed = distRotSpeed(m_rng);
      }

      m_particles.push_back(p);
    }
  }

  ParticleConfig m_config;
  std::vector<Particle> m_particles;
  std::mt19937 m_rng;
};

} // namespace game::systems
