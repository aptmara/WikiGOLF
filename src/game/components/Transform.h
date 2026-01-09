#pragma once
/**
 * @file Transform.h
 * @brief Transform Component
 */

#include <DirectXMath.h>

namespace game::components {

struct Transform {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // Quaternion
    DirectX::XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};

    // ヘルパー: ワールド行列取得
    DirectX::XMMATRIX GetWorldMatrix() const {
        // SRT順序: Scale -> Rotation -> Translation
        DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&position);
        DirectX::XMVECTOR r = DirectX::XMLoadFloat4(&rotation);
        DirectX::XMVECTOR s = DirectX::XMLoadFloat3(&scale);
        
        return DirectX::XMMatrixScalingFromVector(s) *
               DirectX::XMMatrixRotationQuaternion(r) *
               DirectX::XMMatrixTranslationFromVector(p);
    }
};

} // namespace game::components
