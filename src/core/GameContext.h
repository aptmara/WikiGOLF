#pragma once
/**
 * @file GameContext.h
 * @brief ゲーム全体で共有されるコンテキスト（Bridge）
 */

namespace resources {
class ResourceManager;
}
namespace ecs {
class World;
}
namespace graphics {
class GraphicsDevice;
}
namespace core {
class Input;
}
namespace core {
class Input;
}
namespace game::systems {
class AudioSystem;
}

namespace core {
class SceneManager; // 前方宣言
}

namespace core {

struct GameContext {
  resources::ResourceManager &resource;
  ecs::World &world;
  graphics::GraphicsDevice &graphics;
  core::Input &input;
  game::systems::AudioSystem *audio = nullptr; // オーディオシステムへの参照
  core::SceneManager *sceneManager = nullptr;  // シーンマネージャーへの参照

  // シーン遷移や終了リクエスト
  bool shouldClose = false;

  // 時間管理
  float dt = 0.016f; // デルタタイム (秒)
  float time = 0.0f; // ゲーム開始からの経過時間 (秒)

  // コンストラクタ
  GameContext(resources::ResourceManager &r, ecs::World &w,
              graphics::GraphicsDevice &g, core::Input &i)
      : resource(r), world(w), graphics(g), input(i) {}
};

} // namespace core
