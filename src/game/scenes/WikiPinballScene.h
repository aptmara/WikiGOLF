#pragma once
/**
 * @file WikiPinballScene.h
 * @brief WikiPinballのメインゲームシーン
 */

// #include "../../core/GameContext.h"
#include "../../core/Scene.h"
#include "../../ecs/Entity.h"

namespace core {
struct GameContext;
}

namespace game::scenes {

/**
 * @brief WikiPinballのゲームプレイシーンクラス
 */
class WikiPinballScene : public core::Scene {
public:
  /**
   * @brief シーン名を取得
   * @return シーン名文字列
   */
  const char *GetName() const override { return "WikiPinballScene"; }

  /**
   * @brief シーン開始時の初期化処理
   * @param ctx ゲームコンテキスト
   */
  void OnEnter(core::GameContext &ctx) override;

  /**
   * @brief フレームごとの更新処理
   * @param ctx ゲームコンテキスト
   */
  void OnUpdate(core::GameContext &ctx) override;

  /**
   * @brief シーン終了時の後始末
   * @param ctx ゲームコンテキスト
   */
  void OnExit(core::GameContext &ctx) override;

private:
  /**
   * @brief テーブル（Wikiページ）のセットアップ
   */
  void SetupTable(core::GameContext &ctx);

  /**
   * @brief ボールのスポーン処理
   */
  void SpawnBall(core::GameContext &ctx);

  /**
   * @brief 外壁の作成
   */
  void CreateBoundaries(core::GameContext &ctx);

  /**
   * @brief 見出し（障害物）の作成
   * @param x X座標
   * @param z Z座標
   * @param text 表示テキスト
   */
  void CreateHeading(core::GameContext &ctx, float x, float z,
                     const std::wstring &text);

  /**
   * @brief リンク障害物の作成
   * @param x X座標
   * @param z Z座標
   * @param linkTarget リンク先記事タイトル
   */
  void CreateLinkObstacle(core::GameContext &ctx, float x, float z,
                          const std::string &linkTarget);

  /**
   * @brief フリッパーの作成
   */
  void CreateFlippers(core::GameContext &ctx);

  /**
   * @brief 指定ページへ遷移
   * @param pageName 遷移先ページ名
   */
  void TransitionToPage(core::GameContext &ctx, const std::string &pageName);

  ecs::Entity m_ballEntity;        ///< プレイヤーのボールエンティティ
  ecs::Entity m_scoreEntity;       ///< スコア表示UI
  ecs::Entity m_titleEntity;       ///< ページタイトル表示UI
  ecs::Entity m_infoEntity;        ///< 情報パネルUI
  int m_score = 0;                 ///< 現在のスコア
  int m_headingsUnlocked = 0;      ///< アンロックされた見出しの数
  std::wstring m_currentPageTitle; ///< 現在のページタイトル
};

} // namespace game::scenes
