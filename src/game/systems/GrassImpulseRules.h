#pragma once
/**
 * @file GrassImpulseRules.h
 * @brief 芝を揺らす出来事（ショット・着地・カップイン）の寿命と、着地の見分け方
 */

#include "../components/GrassField.h"
#include <algorithm>
#include <cstdint>

namespace game::systems {

/** @brief 出来事が芝を揺らし続ける時間[秒]（GrassFieldVS.hlsl の式と合わせる） */
inline float GrassImpulseDuration(components::GrassImpulseKind kind) {
  switch (kind) {
  case components::GrassImpulseKind::Swing:
    return 1.4f;
  case components::GrassImpulseKind::Landing:
    return 1.6f;
  case components::GrassImpulseKind::CupIn:
  default:
    return 3.0f;
  }
}

/** @brief 出来事を一覧へ足す（いっぱいなら最も古いものを上書きする） */
inline void PushGrassImpulse(components::GrassImpulseEvents &events,
                             const components::GrassImpulse &impulse) {
  constexpr uint32_t capacity = components::GrassImpulseEvents::kCapacity;
  events.items[events.next % capacity] = impulse;
  events.next = (events.next + 1u) % capacity;
  events.count = std::min(events.count + 1u, capacity);
}

/**
 * @brief 時刻 now でまだ揺らしている出来事を out へ詰める
 * @return 詰めた数
 */
inline uint32_t CollectActiveGrassImpulses(
    const components::GrassImpulseEvents &events, float now,
    std::array<components::GrassImpulse, components::GrassImpulseEvents::kCapacity> &out) {
  uint32_t active = 0;
  for (uint32_t i = 0; i < events.count; ++i) {
    const auto &impulse = events.items[i];
    const float age = now - impulse.startTime;
    if (age >= 0.0f && age <= GrassImpulseDuration(impulse.kind)) {
      out[active++] = impulse;
    }
  }
  return active;
}

/** @brief 着地で輪を出す最小の落下の速さ[m/s]（転がりの小さな跳ねでは出さない） */
inline constexpr float kGrassLandingMinFallSpeed = 1.5f;

/** @brief 落下の速さから着地の輪の強さ（0.25〜1） */
inline float GrassLandingStrength(float fallSpeed) {
  return std::clamp(fallSpeed / 14.0f, 0.25f, 1.0f);
}

/** @brief 初速[m/s]からショットのなびきの強さ（0.35〜1） */
inline float GrassSwingStrength(float launchSpeed) {
  return std::clamp(launchSpeed / 45.0f, 0.35f, 1.0f);
}

} // namespace game::systems
