#pragma once
/**
 * @file TangentGenerator.h
 * @brief 頂点の接線・従法線を計算するユーティリティ
 */

#include "Mesh.h"
#include <vector>

namespace graphics {

/// @brief 接線と従法線を三角形から再計算する
void ComputeTangents(std::vector<Vertex> &vertices,
                     const std::vector<uint32_t> &indices);

} // namespace graphics
