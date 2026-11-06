#pragma once
/**
 * @file ArticlePaperEffect.h
 * @brief 記事の冒頭を紙として画面いっぱいに立て、地面へ倒す3D演出
*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include <DirectXMath.h>
#include <functional>
#include <vector>

struct ID3D11ShaderResourceView;

namespace core {
struct GameContext;
}

namespace game::controllers {

/**
 * @brief 記事のタイルを貼った板を管理します。
 * @details ティーは記事の末尾近くにあるため、地面の記事をカメラで映しても冒頭は
 *          見えません。そこで冒頭を貼った板をカメラの前に置きます。記事は512pxごとの
 *          タイルに分かれているので、紙は複数の帯を縦に並べて作ります。
 *          紙の上には旗などの3D物体を貼り付けられ、紙と一緒に倒れます。
*/
class ArticlePaperEffect {
public:
  /** @brief 記事のタイル1枚です。*/
  struct Tile {
    ID3D11ShaderResourceView *texture = nullptr;
    float width = 0.0f;
    float height = 0.0f;
  };

  /** @brief 紙の生成に必要な情報です。*/
  struct Setup {
    std::vector<Tile> tiles; ///< 記事のタイル（上から順）
    DirectX::XMFLOAT3 cameraPosition{};
    DirectX::XMFLOAT3 focusPosition{};
    float cameraFov = 0.0f;
    float cameraAspect = 16.0f / 9.0f;
    std::function<float(float, float)> groundHeight; ///< 地面の高さ (x, z)
  };

  /**
   * @brief 画面上の点に重なる、紙に貼り付けた座標です。
   * @details local は紙の下辺中央からの距離で、x は右、y は紙の上方向、
   *          z はカメラ側です。紙が倒れても同じ値のまま使えます。
  */
  struct Anchor {
    DirectX::XMFLOAT3 local{};
    float metersPerPixel = 0.0f; ///< その奥行きで仮想解像度の1ピクセルが何mか
  };

  /**
   * @brief カメラと正対させて紙を立てます。
   * @return タイルがなく紙を作れないときは false を返します。
  */
  bool Spawn(core::GameContext &ctx, const Setup &setup);

  /**
   * @brief 紙の傾きと不透明度を更新します。
   * @param fallProgress 0で立った状態、1で地面と平行です。
   * @param opacity 不透明度です。貼り付けた物体にも同じ値を使います。
  */
  void Update(core::GameContext &ctx, float fallProgress, float opacity);

  /** @brief 紙と貼り付けた物体を破棄します。*/
  void Shutdown(core::GameContext &ctx);

  /** @brief 紙が立っているか。*/
  bool IsActive() const { return !m_strips.empty(); }

  /**
   * @brief 画面上の点（仮想解像度）に重なる位置を、紙からカメラ側へ浮かせて求めます。
   * @param forward 紙から浮かせる距離（m）です。浮かせても画面上の位置は変わりません。
  */
  Anchor ResolveScreenPoint(float virtualX, float virtualY,
                            float forward) const;

  /**
   * @brief 紙の上に横長の帯を敷きます。記事の文字を隠して図を載せるのに使います。
   * @param color 帯の色です。紙と同じく照明を受けません。
  */
  void AddBand(core::GameContext &ctx, float virtualTop, float virtualBottom,
               const DirectX::XMFLOAT3 &color, float forward);

  /**
   * @brief 物体を紙に貼り付けます。位置と向きは紙と一緒に更新されます。
   * @details 破棄は呼び出し側が行います。不透明度は MeshRenderer の color.w に入れます。
  */
  void Attach(ecs::Entity entity, const DirectX::XMFLOAT3 &local);

  /** @brief 貼り付けた物体の位置を変えます（旗が伸びる動きなど）。*/
  void SetAttachedLocal(ecs::Entity entity, const DirectX::XMFLOAT3 &local);

private:
  /** @brief 紙を構成する帯1枚のEntityです。*/
  struct StripEntity {
    ecs::Entity entity = UINT32_MAX;
    DirectX::XMFLOAT3 local{}; ///< 帯の下辺中央の位置
  };

  /** @brief 紙に貼り付けた物体です。*/
  struct AttachedEntity {
    ecs::Entity entity = UINT32_MAX;
    DirectX::XMFLOAT3 local{};
  };

  DirectX::XMFLOAT3 ToWorld(const DirectX::XMFLOAT3 &local, float angle) const;

  ecs::EntityOwner m_entityOwner;
  std::vector<StripEntity> m_strips;
  std::vector<AttachedEntity> m_attached;
  DirectX::XMFLOAT3 m_hinge{};  ///< 紙の下辺の中央（倒れるときの軸）
  DirectX::XMFLOAT3 m_eye{};    ///< 紙を立てたときのカメラ位置
  float m_standAngle = 0.0f;
  float m_distance = 0.0f;      ///< カメラから紙までの距離
  float m_paperWidth = 0.0f;
  float m_paperHeight = 0.0f;
  float m_visibleWidth = 0.0f;  ///< 画面に映る紙の幅（はみ出し分を除く）
  float m_visibleHeight = 0.0f; ///< 画面に映る紙の高さ（はみ出し分を除く）
};

} // namespace game::controllers
