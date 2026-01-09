#pragma once
/**
 * @file System.h
 * @brief ECSシステムの基底クラス
 * 
 * システムはコンポーネントデータを処理するロジックを持つ。
 * 各システムはUpdate()でフレームごとの処理を行う。
 */

namespace ecs {

class World; // 前方宣言

/// @brief システムの基底クラス
class ISystem {
public:
    virtual ~ISystem() = default;

    /// @brief 初期化処理（World登録後に1回呼ばれる）
    virtual void Initialize([[maybe_unused]] World& world) {}

    /// @brief 毎フレーム呼ばれる更新処理
    /// @param world ECSワールド
    /// @param deltaTime 前フレームからの経過時間（秒）
    virtual void Update(World& world, float deltaTime) = 0;

    /// @brief 終了処理
    virtual void Shutdown([[maybe_unused]] World& world) {}

    /// @brief システムの優先度（小さいほど先に実行）
    virtual int Priority() const { return 0; }
};

} // namespace ecs
