#include "src/core/Logger.h"
#include "src/graphics/ObjLoader.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>


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

// 一時的なOBJファイルを作成
void CreateTestObj(const std::string &path) {
  std::ofstream file(path);
  // シンプルな四角錘（Pyramid）
  file << "# Test Cube\n";
  file << "v 0.0 1.0 0.0\n";    // v1 tip
  file << "v -1.0 -1.0 1.0\n";  // v2 front-left
  file << "v 1.0 -1.0 1.0\n";   // v3 front-right
  file << "v 1.0 -1.0 -1.0\n";  // v4 back-right
  file << "v -1.0 -1.0 -1.0\n"; // v5 back-left

  file << "vt 0.5 1.0\n";
  file << "vt 0.0 0.0\n";
  file << "vt 1.0 0.0\n";

  file << "vn 0.0 1.0 0.0\n";
  file << "vn 0.0 -1.0 0.0\n";

  // f v/vt/vn
  file << "f 1/1/1 2/2/2 3/3/2\n"; // Front face
  file << "f 1/1/1 3/2/2 4/3/2\n"; // Right face
  file << "f 1/1/1 4/2/2 5/3/2\n"; // Back face
  file << "f 1/1/1 5/2/2 2/3/2\n"; // Left face
  file.close();
}

int main() {
  // ロガー初期化（テスト用）
  core::Logger::Instance().Initialize("test_obj_loader.log");

  std::string testFile = "temp_test.obj";
  CreateTestObj(testFile);

  std::cout << "Starting ObjLoader Test...\n";

  std::vector<graphics::Vertex> vertices;
  std::vector<uint32_t> indices;

  bool success = graphics::ObjLoader::Load(testFile, vertices, indices);

  CHECK(success, "Load function returned true");
  CHECK(indices.size() == 12, "Index count is correct (4 faces * 3 vertices)");

  // クリーンアップ
  std::filesystem::remove(testFile);
  core::Logger::Instance().Shutdown();

  std::cout << "All tests passed!\n";
  return 0;
}
