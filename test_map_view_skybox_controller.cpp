#include "src/game/utils/MapViewState.h"
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      return 1;                                                                \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using game::components::Skybox;
  using game::utils::MapViewSkyboxState;

  std::cout << "Running MapViewSkyboxState tests...\n";

  // 通常表示 → マップビュー → 戻る
  Skybox skyboxVisible;
  skyboxVisible.isVisible = true;

  MapViewSkyboxState state;
  state.Reset(skyboxVisible.isVisible);

  state.Sync(false, skyboxVisible); // 状態変化なし
  CHECK(skyboxVisible.isVisible, "No change when staying in normal view");

  state.Sync(true, skyboxVisible); // マップビュー突入
  CHECK(!skyboxVisible.isVisible, "Skybox hidden when entering map view");

  state.Sync(true, skyboxVisible); // 連続呼び出しでも変化しない
  CHECK(!skyboxVisible.isVisible,
        "Skybox stays hidden while remaining in map view");

  state.Sync(false, skyboxVisible); // マップビュー解除
  CHECK(skyboxVisible.isVisible,
        "Skybox restored to cached visibility when leaving map view");

  // 非表示だったスカイボックスをマップビュー経由で復帰
  Skybox skyboxHidden;
  skyboxHidden.isVisible = false;
  state.Reset(skyboxHidden.isVisible);

  state.Sync(true, skyboxHidden);
  CHECK(!skyboxHidden.isVisible,
        "Hidden skybox remains hidden when entering map view");

  state.Sync(false, skyboxHidden);
  CHECK(!skyboxHidden.isVisible,
        "Hidden skybox stays hidden after leaving map view");

  std::cout << "All MapViewSkyboxState tests passed.\n";
  return 0;
}
