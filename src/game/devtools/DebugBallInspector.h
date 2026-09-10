#pragma once

#include <DirectXMath.h>
#include <string>

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugBallInspector {
public:
  void Draw(core::GameContext &ctx);

private:
  DirectX::XMFLOAT3 m_target{};
  bool m_hasTarget = false;
  bool m_resetMotion = true;
  std::string m_result;
};

} // namespace game::debug
