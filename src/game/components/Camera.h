#pragma once
/**
 * @file Camera.h
 * @brief Camera Component
 */

#include <DirectXMath.h>
#include "Transform.h"

namespace game::components {

struct Camera {
    float fov = DirectX::XM_PIDIV4; // 45 degrees
    float nearZ = 0.01f;
    float farZ = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;
    bool isMainCamera = true;

    DirectX::XMMATRIX GetViewMatrix(const Transform& transform) const {
        DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&transform.position);
        DirectX::XMVECTOR rot = DirectX::XMLoadFloat4(&transform.rotation);
        
        // Forward vector from rotation
        DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(
            DirectX::XMVectorSet(0, 0, 1, 0), rot
        );
        DirectX::XMVECTOR up = DirectX::XMVector3Rotate(
            DirectX::XMVectorSet(0, 1, 0, 0), rot
        );

        return DirectX::XMMatrixLookToLH(pos, forward, up);
    }

    DirectX::XMMATRIX GetProjectionMatrix() const {
        return DirectX::XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
    }
};

} // namespace game::components
