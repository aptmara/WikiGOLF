#pragma once
/**
 * @file EntityOwner.h
 * @brief ECS Entityの生成元と破棄責任を対応させる所有クラス
*/

#include "Entity.h"
#include <cstddef>
#include <vector>

namespace ecs {

class World;

/**
 * @brief 一つの機能が生成したEntityを追跡し、一括破棄するクラスです。
 * @details Worldの寿命は所有しません。Worldへアクセスできる終了処理から
 *          DestroyAllを明示的に呼び出してください。
*/
class EntityOwner {
public:
  EntityOwner() = default;
  ~EntityOwner() = default;

  EntityOwner(const EntityOwner &) = delete;
  EntityOwner &operator=(const EntityOwner &) = delete;
  EntityOwner(EntityOwner &&) = default;
  EntityOwner &operator=(EntityOwner &&) = default;

  /**
   * @brief Entityを生成し、この所有者の破棄対象へ追加します。
   * @param world Entityを生成するWorldです。
   * @return 生成したEntityです。
*/
  Entity Create(World &world);

  /**
   * @brief 外部で生成済みのEntityを破棄対象へ追加します。
   * @param entity 所有権を引き受けるEntityです。
*/
  void Track(Entity entity);

  /**
   * @brief 追跡中の生存Entityをすべて破棄し、追跡状態を空にします。
   * @param world Entityを保持するWorldです。
   * @return 実際に破棄したEntity数です。
*/
  std::size_t DestroyAll(World &world);

  /**
   * @brief 指定したEntityをこの所有者が追跡しているか判定します。
   * @param entity 確認するEntityです。
   * @return 追跡している場合はtrueです。
*/
  bool Owns(Entity entity) const;

  /**
   * @brief 追跡しているEntity数を返します。
   * @return 破棄済みでまだ整理されていないEntityを含む追跡数です。
*/
  std::size_t GetTrackedCount() const { return m_entities.size(); }

private:
  std::vector<Entity> m_entities;
};

} // namespace ecs
