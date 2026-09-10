#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>

namespace ecs {
class World;
}

namespace game::debug {

struct DebugGolfStateSnapshot {
  bool available = false;
  std::string currentPage;
  std::string targetPage;
  std::string material;
  DirectX::XMFLOAT2 windDirection{};
  DirectX::XMFLOAT3 lastShotPosition{};
  float windSpeed = 0.0f;
  float ballSpeed = 0.0f;
  int shotCount = 0;
  int par = 0;
  int moveCount = 0;
  bool grounded = false;
  bool outOfBounds = false;
  bool canShoot = false;
  bool mapView = false;
  bool gameCleared = false;
};

struct DebugShotStateSnapshot {
  bool available = false;
  std::string phase;
  std::string judgement;
  float powerGauge = 0.0f;
  float powerDirection = 0.0f;
  float confirmedPower = 0.0f;
  float impactGauge = 0.0f;
  float impactPerfectCenter = 0.0f;
  float confirmedImpact = 0.0f;
  float maxPower = 0.0f;
  float resultDisplayTime = 0.0f;
};

struct DebugBallPhysicsSnapshot {
  bool available = false;
  uint32_t entity = 0;
  DirectX::XMFLOAT3 position{};
  DirectX::XMFLOAT3 velocity{};
  DirectX::XMFLOAT3 acceleration{};
  DirectX::XMFLOAT3 angularVelocity{};
  float speed = 0.0f;
  float mass = 0.0f;
  float drag = 0.0f;
  float rollingFriction = 0.0f;
  float restitution = 0.0f;
  float spinDecay = 0.0f;
};

struct DebugGameplaySnapshot {
  DebugGolfStateSnapshot golf;
  DebugShotStateSnapshot shot;
  DebugBallPhysicsSnapshot ball;
};

DebugGameplaySnapshot CaptureGameplaySnapshot(ecs::World &world);

} // namespace game::debug
