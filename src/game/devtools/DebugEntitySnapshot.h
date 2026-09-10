#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>

namespace ecs {
class World;
}

namespace game::debug {

struct DebugEntitySnapshot {
  uint32_t entity = 0;
  bool alive = false;
  bool hasTransform = false;
  DirectX::XMFLOAT3 position{};
  DirectX::XMFLOAT4 rotation{};
  DirectX::XMFLOAT3 scale{};
  bool hasRigidBody = false;
  DirectX::XMFLOAT3 velocity{};
  DirectX::XMFLOAT3 acceleration{};
  DirectX::XMFLOAT3 angularVelocity{};
  float mass = 0.0f;
  float drag = 0.0f;
  float rollingFriction = 0.0f;
  float restitution = 0.0f;
  float spinDecay = 0.0f;
  bool isStatic = false;
  bool hasCollider = false;
  std::string colliderType;
  float colliderRadius = 0.0f;
  DirectX::XMFLOAT3 colliderSize{};
  DirectX::XMFLOAT3 colliderOffset{};
  bool hasHeading = false;
  std::string headingText;
  std::string headingLink;
  int headingHealth = 0;
  int headingMaximumHealth = 0;
  bool headingDestroyed = false;
  bool hasGolfHole = false;
  std::string holeLink;
  float holeRadius = 0.0f;
  float holeGravity = 0.0f;
  bool targetHole = false;
};

DebugEntitySnapshot CaptureDebugEntitySnapshot(ecs::World &world,
                                               uint32_t entity);

} // namespace game::debug
