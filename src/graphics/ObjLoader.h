#pragma once
#include "Mesh.h"
#include <string>
#include <vector>


namespace graphics {

/// @brief OBJ形式の3Dモデルローダー
class ObjLoader {
public:
  /// @brief OBJファイルをロードする
  /// @param path ファイルパス
  /// @param outVertices 頂点データの出力先
  /// @param outIndices インデックスデータの出力先
  /// @return 成功時 true
  static bool Load(const std::string &path, std::vector<Vertex> &outVertices,
                   std::vector<uint32_t> &outIndices);
};

} // namespace graphics
