#pragma once
/**
 * @file StartTeePin.h
 * @brief スタートティー用ピン生成ユーティリティ
*/

#include "../../ecs/Entity.h"
#include <DirectXMath.h>

namespace core {
struct GameContext;
}

namespace game::utils {

/** @brief スタートティーピンの地面からの高さ(ワールド単位)。ボールをこの高さの上へ乗せて固定する。*/
constexpr float kStartTeePinHeight = 0.3f;

/**
 * @brief 生成したスタートティーピンのEntityです。
*/
struct StartTeePinResult {
  ecs::Entity entity = UINT32_MAX;
};

/**
 * @brief タイトル画面でWikipediaパズル地球儀を支えるティーと同じ意匠
 * （白い球体を縦に伸ばして棒状に見立てたもの）のピンを、指定した地面位置に立てます。
 * @param ctx ゲームコンテキスト
 * @param groundPosition ピンの根元（地面）位置
 * @return 生成したピンのEntity
*/
StartTeePinResult CreateStartTeePin(core::GameContext &ctx,
                                    const DirectX::XMFLOAT3 &groundPosition);

} // namespace game::utils
