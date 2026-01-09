#pragma once
/**
 * @file FbxLoader.h
 * @brief Assimpを使用したFBX/汎用モデルローダー
 */

#include "Mesh.h"
#include <string>
#include <vector>

namespace graphics {

/// @brief Assimpを使用したモデルローダー
/// @details FBX, OBJ, glTF等の多様なフォーマットに対応
class FbxLoader {
public:
  /// @brief モデルファイルをロードする
  /// @param path ファイルパス
  /// @param outVertices 頂点データの出力先
  /// @param outIndices インデックスデータの出力先
  /// @return 成功時 true
  static bool Load(const std::string &path, std::vector<Vertex> &outVertices,
                   std::vector<uint32_t> &outIndices);
};

} // namespace graphics
