#include "src/core/GameContext.h"
#include "src/core/Input.h"
#include "src/core/Scene.h"
#include "src/ecs/EntityOwner.h"
#include "src/ecs/World.h"
#include "src/graphics/GraphicsDevice.h"
#include "src/resources/ResourceManager.h"
#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(condition, message)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                             \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (0)

namespace resources {

// このテストではResourceManagerを利用しないため、GraphicsDevice参照だけを
// 初期化する最小構成にする。製品側のリソース読み込み処理はリンクしない。
ResourceManager::ResourceManager(graphics::GraphicsDevice &device)
    : m_device(device), m_meshPool(graphics::Mesh{}),
      m_shaderPool(graphics::Shader{}), m_audioPool(audio::AudioClip{}) {}

} // namespace resources

namespace {

struct TestComponent {
  int value = 0;
};

class TestScene final : public core::Scene {
public:
  const char *GetName() const override { return "TestScene"; }

  void OnEnter(core::GameContext &) override {}

  ecs::Entity CreateOwnedEntity(ecs::World &world) {
    return CreateEntity(world);
  }
};

} // namespace

int main() {
  graphics::GraphicsDevice graphics;
  resources::ResourceManager resources(graphics);
  ecs::World world;
  core::Input input;
  core::GameContext context(resources, world, graphics, input);
  TestScene scene;

  const ecs::Entity externalEntity = world.CreateEntity();
  world.Add<TestComponent>(externalEntity, TestComponent{10});

  const ecs::Entity firstOwnedEntity = scene.CreateOwnedEntity(world);
  const ecs::Entity secondOwnedEntity = scene.CreateOwnedEntity(world);
  world.Add<TestComponent>(firstOwnedEntity, TestComponent{20});
  world.Add<TestComponent>(secondOwnedEntity, TestComponent{30});

  CHECK_TRUE(scene.OwnsEntity(firstOwnedEntity),
             "Sceneが作成したEntityを所有対象として記録する");
  CHECK_TRUE(scene.OwnsEntity(secondOwnedEntity),
             "Sceneが複数のEntityを所有対象として記録する");
  CHECK_TRUE(!scene.OwnsEntity(externalEntity),
             "Scene外で作成したEntityを所有対象に含めない");
  CHECK_TRUE(world.GetEntityCount() == 3,
             "終了前は所有Entityと外部Entityが存在する");

  scene.OnExit(context);

  CHECK_TRUE(!world.IsAlive(firstOwnedEntity),
             "Scene終了時に最初の所有Entityを破棄する");
  CHECK_TRUE(!world.IsAlive(secondOwnedEntity),
             "Scene終了時に2番目の所有Entityを破棄する");
  CHECK_TRUE(world.IsAlive(externalEntity),
             "Scene終了時に外部Entityを破棄しない");
  CHECK_TRUE(world.Get<TestComponent>(externalEntity) != nullptr,
             "外部EntityのComponentを維持する");
  CHECK_TRUE(world.GetEntityCount() == 1,
             "Scene終了後は外部Entityだけが残る");

  scene.OnExit(context);
  CHECK_TRUE(world.IsAlive(externalEntity),
             "Scene終了処理を再実行しても外部Entityを維持する");

  ecs::EntityOwner featureOwner;
  const ecs::Entity retiredFeatureEntity = featureOwner.Create(world);
  const ecs::Entity activeFeatureEntity = featureOwner.Create(world);
  world.DestroyEntity(retiredFeatureEntity);

  CHECK_TRUE(featureOwner.GetTrackedCount() == 2,
             "途中で破棄したEntityを含めて機能単位の所有関係を保持する");
  CHECK_TRUE(featureOwner.DestroyAll(world) == 1,
             "一括破棄では現在生存している所有Entityだけを破棄する");
  CHECK_TRUE(!world.IsAlive(activeFeatureEntity),
             "機能単位の所有Entityを一括破棄する");
  CHECK_TRUE(world.IsAlive(externalEntity),
             "機能単位の一括破棄でも外部Entityを維持する");
  CHECK_TRUE(featureOwner.GetTrackedCount() == 0,
             "一括破棄後に所有記録を空にする");

  std::cout << "All scene entity lifecycle tests passed!\n";
  return 0;
}
