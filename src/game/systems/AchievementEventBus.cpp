#include "AchievementEventBus.h"

namespace game::systems {

void AchievementEventBus::Subscribe(Listener listener) {
  m_listeners.push_back(std::move(listener));
}

void AchievementEventBus::Publish(const AchievementEvent &event) {
  for (const auto &listener : m_listeners) {
    listener(event);
  }
}

} // namespace game::systems
