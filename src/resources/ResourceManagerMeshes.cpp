/**
 * @file ResourceManagerMeshes.cpp
 * @brief 統合リソース管理クラスの実装
*/

#include "ResourceManager.h"
#include "ResourceManagerInternals.h"
#include "../core/Logger.h"
#include "../graphics/FbxLoader.h"
#include "../graphics/GraphicsDevice.h"
#include "../graphics/MeshPrimitives.h"
#include "../graphics/ObjLoader.h"
#include <algorithm>
#include <chrono>
#include <vector>

namespace resources {

MeshHandle ResourceManager::LoadMesh(const std::string &path) {
  const auto startedAt = std::chrono::steady_clock::now();
  // LOG_DEBUG("Resource", "LoadMesh: START {}", path.c_str());
  // キャッシュヒット確認
  if (auto it = m_meshCache.find(path); it != m_meshCache.end()) {
    // LOG_DEBUG("Resource", "LoadMesh: Cache hit for {}", path.c_str());
    if (m_meshPool.Get(it->second)) { // ハンドル有効性確認
      // LOG_DEBUG("Resource", "LoadMesh cache hit: {} ({} ms)", path,
      //           ElapsedMs(startedAt));
      return it->second;
    }
    // LOG_DEBUG("Resource", "LoadMesh: Cache handle invalid for {}", path.c_str());
  }

  // LOG_DEBUG("Resource", "LoadMesh: Creating new mesh for {}", path.c_str());
  graphics::Mesh mesh;
  bool success = false;

  // キャッシュに存在しない場合は標準的な各種プリミティブメッシュを生成
  if (path == "builtin/cube" || path == "cube") {
    mesh = graphics::MeshPrimitives::CreateCube(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/sphere" || path == "sphere") {
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/sphere_smooth") {
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice(), 24);
    success = true;
  } else if (path == "builtin/triangle") {
    mesh = graphics::MeshPrimitives::CreateTriangle(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/plane" || path == "plane") {
    mesh =
        graphics::MeshPrimitives::CreatePlane(m_device.GetDevice(), 1.0f, 1.0f);
    success = true;
  } else if (path == "builtin/quad" || path == "quad") {
    mesh = graphics::MeshPrimitives::CreateQuad(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/cylinder" || path == "cylinder") {
    mesh = graphics::MeshPrimitives::CreateCylinder(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/cylinder_smooth") {
    mesh = graphics::MeshPrimitives::CreateCylinder(m_device.GetDevice(), 24);
    success = true;
  } else if (path == "builtin/rock" || path == "rock") {
    mesh = graphics::MeshPrimitives::CreateRock(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/grass_clump" || path == "grass_clump") {
    mesh = graphics::MeshPrimitives::CreateGrassClump(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/grass_patch" || path == "grass_patch") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/grass_patch_0") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 0);
    success = true;
  } else if (path == "builtin/grass_patch_1") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 1);
    success = true;
  } else if (path == "builtin/grass_patch_2") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 2);
    success = true;
  } else if (path == "builtin/grass_patch_3") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 3);
    success = true;
  } else if (path == "builtin/grass_patch_low_0") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 0,
                                                       6, 1);
    success = true;
  } else if (path == "builtin/grass_patch_low_1") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 1,
                                                       6, 1);
    success = true;
  } else if (path == "builtin/grass_patch_low_2") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 2,
                                                       6, 1);
    success = true;
  } else if (path == "builtin/grass_patch_low_3") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 3,
                                                       6, 1);
    success = true;
  } else if (path == "builtin/grass_patch_medium_0") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 0,
                                                       8, 1);
    success = true;
  } else if (path == "builtin/grass_patch_medium_1") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 1,
                                                       8, 1);
    success = true;
  } else if (path == "builtin/grass_patch_medium_2") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 2,
                                                       8, 1);
    success = true;
  } else if (path == "builtin/grass_patch_medium_3") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 3,
                                                       8, 1);
    success = true;
  } else if (path == "builtin/grass_patch_ultra_0") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 0,
                                                       12, 3);
    success = true;
  } else if (path == "builtin/grass_patch_ultra_1") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 1,
                                                       12, 3);
    success = true;
  } else if (path == "builtin/grass_patch_ultra_2") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 2,
                                                       12, 3);
    success = true;
  } else if (path == "builtin/grass_patch_ultra_3") {
    mesh = graphics::MeshPrimitives::CreateGrassPatch(m_device.GetDevice(), 3,
                                                       12, 3);
    success = true;
  } else if (path == "builtin/turf_patch_0") {
    mesh = graphics::MeshPrimitives::CreateTurfPatch(m_device.GetDevice(), 0);
    success = true;
  } else if (path == "builtin/turf_patch_1") {
    mesh = graphics::MeshPrimitives::CreateTurfPatch(m_device.GetDevice(), 1);
    success = true;
  } else if (path == "builtin/turf_patch_2") {
    mesh = graphics::MeshPrimitives::CreateTurfPatch(m_device.GetDevice(), 2);
    success = true;
  } else if (path == "builtin/turf_patch_3") {
    mesh = graphics::MeshPrimitives::CreateTurfPatch(m_device.GetDevice(), 3);
    success = true;
  } else if (path == "builtin/turf_patch_dense_0") {
    mesh =
        graphics::MeshPrimitives::CreateDenseTurfPatch(m_device.GetDevice(), 0);
    success = true;
  } else if (path == "builtin/turf_patch_dense_1") {
    mesh =
        graphics::MeshPrimitives::CreateDenseTurfPatch(m_device.GetDevice(), 1);
    success = true;
  } else if (path == "builtin/turf_patch_dense_2") {
    mesh =
        graphics::MeshPrimitives::CreateDenseTurfPatch(m_device.GetDevice(), 2);
    success = true;
  } else if (path == "builtin/turf_patch_dense_3") {
    mesh =
        graphics::MeshPrimitives::CreateDenseTurfPatch(m_device.GetDevice(), 3);
    success = true;
  } else if (path == "builtin/turf_patch_ultra_0") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseTurfPatch(
        m_device.GetDevice(), 0);
    success = true;
  } else if (path == "builtin/turf_patch_ultra_1") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseTurfPatch(
        m_device.GetDevice(), 1);
    success = true;
  } else if (path == "builtin/turf_patch_ultra_2") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseTurfPatch(
        m_device.GetDevice(), 2);
    success = true;
  } else if (path == "builtin/turf_patch_ultra_3") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseTurfPatch(
        m_device.GetDevice(), 3);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_dense_0") {
    mesh = graphics::MeshPrimitives::CreateDenseFairwayTurfPatch(
        m_device.GetDevice(), 0);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_dense_1") {
    mesh = graphics::MeshPrimitives::CreateDenseFairwayTurfPatch(
        m_device.GetDevice(), 1);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_dense_2") {
    mesh = graphics::MeshPrimitives::CreateDenseFairwayTurfPatch(
        m_device.GetDevice(), 2);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_dense_3") {
    mesh = graphics::MeshPrimitives::CreateDenseFairwayTurfPatch(
        m_device.GetDevice(), 3);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_ultra_0") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseFairwayTurfPatch(
        m_device.GetDevice(), 0);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_ultra_1") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseFairwayTurfPatch(
        m_device.GetDevice(), 1);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_ultra_2") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseFairwayTurfPatch(
        m_device.GetDevice(), 2);
    success = true;
  } else if (path == "builtin/fairway_turf_patch_ultra_3") {
    mesh = graphics::MeshPrimitives::CreateUltraDenseFairwayTurfPatch(
        m_device.GetDevice(), 3);
    success = true;
  } else if (path == "builtin/sand_crater" || path == "sand_crater") {
    mesh = graphics::MeshPrimitives::CreateSandCrater(m_device.GetDevice());
    success = true;
  } else if (path == "builtin/torus" || path == "torus") {
    // トーラス生成未対応時は球体で代替
    mesh = graphics::MeshPrimitives::CreateSphere(m_device.GetDevice());
    success = true;
  } else {
    // LOG_DEBUG("Resource", "LoadMesh: Loading from file {}", path.c_str());
    // ファイル拡張子を判定してローダーを選択
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t> indices;

    // 拡張子を小文字で取得
    std::string extension;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
      extension = path.substr(dotPos);
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     ::tolower);
    }

    bool loaded = false;

    // FBX/glTF/3DS/DAE等はFbxLoader(Assimp)を使用
    if (extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
        extension == ".3ds" || extension == ".dae" || extension == ".blend") {
      loaded = graphics::FbxLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "FBX/Assimp Load failed: {}", path.c_str());
      }
    }
    // OBJファイルは専用ローダーを使用
    else if (extension == ".obj" || extension.empty()) {
      loaded = graphics::ObjLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "OBJ Load failed: {}", path.c_str());
      }
    } else {
      // 不明な拡張子は一応Assimpで試みる
      loaded = graphics::FbxLoader::Load(path, vertices, indices);
      if (!loaded) {
        LOG_ERROR("Resource", "Unknown format load failed: {}", path.c_str());
      }
    }

    if (loaded) {
      if (mesh.Create(m_device.GetDevice(), vertices, indices)) {
        success = true;
        LOG_INFO("Resource", "Loaded Mesh: {} ({} vertices, {} ms)",
                 path.c_str(), vertices.size(), ElapsedMs(startedAt));
      }
    }
  }

  if (!success) {
    LOG_ERROR("Resource", "Mesh load failed or fallback triggered: {}",
              path.c_str());
    // 失敗時はCubeで代用
    mesh = graphics::MeshPrimitives::CreateCube(m_device.GetDevice());
  }

  auto handle = m_meshPool.Add(std::move(mesh));
  m_meshCache[path] = handle;
  if (success) {
    // LOG_DEBUG("Resource", "LoadMesh finished: {} ({} ms)", path,
    //           ElapsedMs(startedAt));
  } else {
    LOG_WARN("Resource", "LoadMesh fallback finished: {} ({} ms)", path,
             ElapsedMs(startedAt));
  }
  return handle;
}

MeshHandle ResourceManager::CreateDynamicMesh(
    const std::string &name, const std::vector<graphics::Vertex> &vertices,
    const std::vector<uint32_t> &indices) {

  // 重複キャッシュ時は最新データで動的メッシュを上書き作成

  graphics::Mesh mesh;
  if (!mesh.Create(m_device.GetDevice(), vertices, indices)) {
    LOG_ERROR("Resource", "Failed to create dynamic mesh: {}", name);
    return {};
  }

  // キャッシュを新規データで置換し、古いデータの解放は一括クリーンアップに委ねる

  auto handle = m_meshPool.Add(std::move(mesh));
  m_meshCache[name] = handle;

  LOG_DEBUG("Resource", "Created dynamic mesh: {} ({} vertices)", name,
           vertices.size());
  return handle;
}

MeshHandle ResourceManager::FindMesh(const std::string &name) const {
  if (auto it = m_meshCache.find(name); it != m_meshCache.end()) {
    return it->second;
  }
  return MeshHandle::Invalid();
}


} // namespace resources

