#pragma once
/**
 * @file TitleScene.h
 * @brief タイトル画面シーン（WIKI GOLF 3Dスタイル）
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"
#include "../../graphics/TextStyle.h"
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <future>

namespace game::scenes {

/**
 * @brief ゲームのタイトル画面シーンクラス
 *
 * 添付画像（WIKI GOLF）のビジュアルを 3D エンジン上で再現する。
 * 構成要素:
 *   - GolfCourseClear スカイボックス（晴天・白雲・青空）
 *   - ゴルフコース地面（緑フェアウェイ）
 *   - Wikipedia パズル地球儀（ティー台に設置、緩やかに回転）
 *   - 旗エンティティ（右手側）
 *   - タイトルロゴ UI（画面上部中央）
 *   - 右パネルメニュー UI（6 項目）
 *   - 下部ステータスバー UI
 */
class TitleScene : public core::Scene {
public:
  const char *GetName() const override { return "TitleScene"; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;

private:
  // --- 3D エンティティ ---
  ecs::Entity m_cameraEntity  = 0; ///< カメラ
  ecs::Entity m_skyboxEntity  = 0; ///< スカイボックス
  ecs::Entity m_floorEntity   = 0; ///< ゴルフコース地面
  ecs::Entity m_globeEntity   = 0; ///< Wikipedia パズル地球儀
  ecs::Entity m_teeLoEntity   = 0; ///< ティー台・支柱（下）
  ecs::Entity m_teeHiEntity   = 0; ///< ティー台・カップ（上）
  ecs::Entity m_flagEntity    = 0; ///< 旗

  // --- メニュー UI ---
  /**
   * @brief メニュー項目 1 つ分の状態
   * @note UIText エンティティ + ホバーアニメーション制御
   */
  struct MenuEntry {
    ecs::Entity entity;          ///< UIText エンティティ (背景・ラベル)
    ecs::Entity iconEntity = 0;  ///< UIText エンティティ (アイコン)
    std::wstring label;          ///< ラベル文字列
    float baseY;                 ///< Y 座標ベース値
    bool isHovered = false;      ///< ホバー状態
  };
  std::vector<MenuEntry> m_menuEntries;

  // --- 演出制御 ---
  float m_time = 0.0f;

  // --- ポップアップ演出 ---
  ecs::Entity m_popupBgEntity = 0;
  ecs::Entity m_popupTextEntity = 0;
  float m_popupTimer = 0.0f;

  // --- 状態とコース選択UI ---
  enum class TitleState { MainMenu, CourseSelect };
  TitleState m_state = TitleState::MainMenu;

  ecs::Entity m_csBgEntity = 0;
  ecs::Entity m_csTitleEntity = 0;
  
  ecs::Entity m_startInputBg = 0;
  ecs::Entity m_startInputText = 0;
  ecs::Entity m_startPasteBtn = 0;
  std::wstring m_startString = L"";

  ecs::Entity m_goalInputBg = 0;
  ecs::Entity m_goalInputText = 0;
  ecs::Entity m_goalPasteBtn = 0;
  std::wstring m_goalString = L"";

  int m_focusIndex = 0; // 0:なし, 1:スタート, 2:ゴール

  ecs::Entity m_checkBtn = 0;
  ecs::Entity m_startBtn = 0;
  ecs::Entity m_closeBtn = 0;

  ecs::Entity m_previewBg = 0;
  ecs::Entity m_previewText = 0;

  std::future<std::string> m_checkTask;
  bool m_checking = false;
  bool m_readyToStart = false;

  void CreateCourseSelectUI(core::GameContext& ctx);
  void SetMainMenuVisible(core::GameContext& ctx, bool visible);
  void SetCourseSelectVisible(core::GameContext& ctx, bool visible);
  void UpdateCourseSelect(core::GameContext& ctx);
};

} // namespace game::scenes
