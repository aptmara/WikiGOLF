#include "src/resources/ResourcePool.h"
#include <cassert>
#include <iostream>
#include <vector>


// テスト用ユーティリティ
#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      std::exit(1);                                                            \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

struct TestResource {
  int value;
  char
      padding[1024]; // サイズを大きくしてリアロケーションの影響を受けやすくする
};

int main() {
  std::cout << "Starting ResourcePool Safety Test...\n";

  // ダミーリソース
  TestResource dummy{-1};
  resources::ResourcePool<TestResource> pool(std::move(dummy));

  // 1. 最初のリソースを追加
  auto handle1 = pool.Add({1});
  TestResource *ptr1 = pool.Get(handle1);

  std::cout << "Initial pointer: " << ptr1 << "\n";

  // 2. リアロケーションが発生するまで追加
  // ResourcePoolの初期予約サイズは128なので、それ以上追加する
  std::cout << "Adding resources to trigger reallocation...\n";
  for (int i = 0; i < 200; ++i) {
    pool.Add({i + 2});
  }

  // 3. 最初のポインタがまだ有効か確認
  TestResource *ptr1_after = pool.Get(handle1);
  std::cout << "Pointer after reallocation: " << ptr1_after << "\n";

  // アドレスが変わっていないことを確認したい
  // vectorの場合は変わるはず -> "FAIL" となるはず
  if (ptr1 == ptr1_after) {
    std::cout << "[PASS] Pointers are stable.\n";
  } else {
    std::cerr << "[FAIL] Pointers CHANGED! Dangling pointer risk detected.\n";
    std::cerr << "Original: " << ptr1 << ", Current: " << ptr1_after << "\n";
    // 今はフェーズ分けのため、ここでexit(1)にするのが正しい挙動
    return 1;
  }

  return 0;
}
