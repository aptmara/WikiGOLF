/**
 * @file FbxLoader.cpp
 * @brief Assimpを使用したFBX/汎用モデルローダーの実装
 */

#include "FbxLoader.h"
#include "../core/Logger.h"
#include "TangentGenerator.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace graphics {

/// @brief メッシュノードを再帰的に処理する
/// @param mesh Assimpのメッシュ
/// @param outVertices 頂点出力先
/// @param outIndices インデックス出力先
/// @param baseIndex 現在のベースインデックス
static void ProcessMesh(const aiMesh *mesh, std::vector<Vertex> &outVertices,
                        std::vector<uint32_t> &outIndices, uint32_t baseIndex) {
  // 頂点データの抽出
  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    Vertex vertex{};

    // 位置
    vertex.position.x = mesh->mVertices[i].x;
    vertex.position.y = mesh->mVertices[i].y;
    vertex.position.z = mesh->mVertices[i].z;

    // 法線
    if (mesh->HasNormals()) {
      vertex.normal.x = mesh->mNormals[i].x;
      vertex.normal.y = mesh->mNormals[i].y;
      vertex.normal.z = mesh->mNormals[i].z;
    } else {
      vertex.normal = {0.0f, 1.0f, 0.0f};
    }

    // テクスチャ座標
    if (mesh->HasTextureCoords(0)) {
      vertex.texCoord.x = mesh->mTextureCoords[0][i].x;
      vertex.texCoord.y = mesh->mTextureCoords[0][i].y;
    } else {
      vertex.texCoord = {0.0f, 0.0f};
    }

    // 頂点カラー（あれば）、なければ白
    if (mesh->HasVertexColors(0)) {
      vertex.color.x = mesh->mColors[0][i].r;
      vertex.color.y = mesh->mColors[0][i].g;
      vertex.color.z = mesh->mColors[0][i].b;
      vertex.color.w = mesh->mColors[0][i].a;
    } else {
      vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    outVertices.push_back(vertex);
  }

  // インデックスの抽出
  for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
    const aiFace &face = mesh->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j++) {
      outIndices.push_back(baseIndex + face.mIndices[j]);
    }
  }
}

/// @brief ノードを再帰的に処理する
/// @param node 現在のノード
/// @param scene シーン全体
/// @param outVertices 頂点出力先
/// @param outIndices インデックス出力先
static void ProcessNode(const aiNode *node, const aiScene *scene,
                        std::vector<Vertex> &outVertices,
                        std::vector<uint32_t> &outIndices) {
  // このノードが持つメッシュを処理
  for (unsigned int i = 0; i < node->mNumMeshes; i++) {
    const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
    uint32_t baseIndex = static_cast<uint32_t>(outVertices.size());
    ProcessMesh(mesh, outVertices, outIndices, baseIndex);
  }

  // 子ノードを再帰処理
  for (unsigned int i = 0; i < node->mNumChildren; i++) {
    ProcessNode(node->mChildren[i], scene, outVertices, outIndices);
  }
}

bool FbxLoader::Load(const std::string &path, std::vector<Vertex> &outVertices,
                     std::vector<uint32_t> &outIndices) {
  Assimp::Importer importer;

  // インポート設定
  // - 三角形化
  // - 法線生成
  // - UV座標のフリップ（DirectX用）
  // - 接線・従接線生成
  unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals |
                       aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
                       aiProcess_JoinIdenticalVertices;

  const aiScene *scene = importer.ReadFile(path, flags);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    LOG_ERROR("FbxLoader", "Assimpエラー: {}", importer.GetErrorString());
    return false;
  }

  outVertices.clear();
  outIndices.clear();

  // ルートノードから再帰的に処理
  ProcessNode(scene->mRootNode, scene, outVertices, outIndices);

  ComputeTangents(outVertices, outIndices);

  LOG_INFO("FbxLoader", "ロード成功: {} (頂点: {}, インデックス: {})", path,
           outVertices.size(), outIndices.size());

  return true;
}

} // namespace graphics
