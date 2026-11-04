#pragma once
/**
 * @file FbxLoader.h
 * @brief Assimpを使用したFBX/汎用モデルローダー
*/

#include "Mesh.h"
#include <cstdint>
#include <string>
#include <vector>

namespace graphics {

/** @brief モデルに埋め込まれた画像（png/jpg 等の圧縮バイト列のまま）。 */
struct EmbeddedModelTextures {
  std::vector<uint8_t> baseColor;
  std::vector<uint8_t> normal;
};

/**
 * @brief Assimpを使用したモデルローダー
 * @details FBX, OBJ, glTF等の多様なフォーマットに対応
 */
class FbxLoader {
public:
  /**
   * @brief モデルファイルをロードする
   * @param path ファイルパス
   * @param outVertices 頂点データの出力先
   * @param outIndices インデックスデータの出力先
   * @return 成功時 true
   */
  static bool Load(const std::string &path, std::vector<Vertex> &outVertices,
                   std::vector<uint32_t> &outIndices);

  /**
   * @brief GLB 等に埋め込まれたベースカラーと法線マップの画像を取り出す
   * @param path ファイルパス
   * @param out 画像の出力先（モデルが持たない種類は空のまま）
   * @return ファイルを読めたら true
   */
  static bool LoadEmbeddedTextures(const std::string &path,
                                   EmbeddedModelTextures &out);
};

} // namespace graphics
