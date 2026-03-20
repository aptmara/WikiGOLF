void WikiGolfScene::InitializeUI(core::GameContext &ctx,
                                 game::components::GolfGameState &state) {
  LOG_INFO("WikiGolf", "Initializing UI elements (Browser HUD v2)...");

  // =========================================================
  // ミニマップUI (右上 x=1040, y=20, 220x220)
  // =========================================================
  if (m_minimapRenderer) {
    m_minimapEntity = CreateEntity(ctx.world);
    auto &ui = ctx.world.Add<UIImage>(m_minimapEntity);
    ui.textureSRV = m_minimapRenderer->GetSRV();
    ui.width = 220.0f;
    ui.height = 220.0f;
    ui.x = 1040.0f;
    ui.y = 20.0f;
    ui.visible = true;
    ui.layer = 100;

    // 現在地マーカー（パルスアニメーション対応）
    m_minimapMarkerEntity = CreateEntity(ctx.world);
    auto &marker = ctx.world.Add<UIText>(m_minimapMarkerEntity);
    marker.text = L"◎";
    marker.x = ui.x + ui.width * 0.5f - 10.0f;
    marker.y = ui.y + ui.height * 0.5f - 10.0f;
    marker.style = graphics::TextStyle::Guide();
    marker.style.fontSize = 22.0f;
    marker.style.color = {1.0f, 0.9f, 0.2f, 1.0f};
    marker.layer = ui.layer + 1;
  }

  // =========================================================
  // 提案2: 風カード (右上 ミニマップ下: x=1040, y=248)
  // カード背景はUITextのbgColorで実現
  // =========================================================
  constexpr float kWindCardX = 1040.0f;
  constexpr float kWindCardY = 248.0f;
  constexpr float kWindCardW = 220.0f;

  // "WIND" ラベル（カード背景込み）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"WIND";
    t.x = kWindCardX + 10.0f;
    t.y = kWindCardY + 8.0f;
    t.width = kWindCardW - 20.0f;
    t.height = 20.0f;
    t.style = graphics::TextStyle::CardLabel();
    // カード背景（このラベルで代表して背景描画）
    t.style.bgColor = {0.059f, 0.090f, 0.165f, 0.88f};
    t.style.cornerRadius = 10.0f;
    t.style.borderWidth = 1.0f;
    t.style.borderColor = {0.220f, 0.380f, 0.600f, 0.4f};
    t.visible = true;
    t.layer = 100;
    state.windCardLabelEntity = e;
  }

  // 風速数値（大きく）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"-- m/s";
    t.x = kWindCardX + 10.0f;
    t.y = kWindCardY + 30.0f;
    t.width = 120.0f;
    t.height = 38.0f;
    t.style = graphics::TextStyle::CardValue();
    t.visible = true;
    t.layer = 101;
    state.windCardValueEntity = e;
  }

  // 方向矢印 + 向き（右側に配置）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"→";
    t.x = kWindCardX + 130.0f;
    t.y = kWindCardY + 30.0f;
    t.width = 80.0f;
    t.height = 38.0f;
    t.style = graphics::TextStyle::CardValue();
    t.style.color = {0.220f, 0.745f, 0.973f, 1.0f}; // 風矢印はスカイブルー
    t.style.fontSize = 32.0f;
    t.style.align = graphics::TextAlign::Center;
    t.visible = true;
    t.layer = 101;
    state.windCardUnitEntity = e;
  }

  // 既存互換エンティティ（windEntityはLoadPage側で使う参照が残るため空で保持）
  {
    auto windE = CreateEntity(ctx.world);
    auto &wt = ctx.world.Add<UIText>(windE);
    wt.visible = false; // 新UIに移行したため非表示
    state.windEntity = windE;
  }

  // 既存互換: 風矢印画像（LoadPage側でrotation設定される参照が残るため保持）
  {
    auto windArrowE = CreateEntity(ctx.world);
    auto &wa = ctx.world.Add<UIImage>(windArrowE);
    wa = UIImage::Create("", 0.0f, 0.0f);
    wa.width = 0.0f;
    wa.height = 0.0f;
    wa.visible = false; // 新UIでは不使用
    state.windArrowEntity = windArrowE;
  }

  // =========================================================
  // 提案1: ブラウザ風HUD (左上)
  // 構成:
  //  🌐 [現在ページ]  →  [ターゲットページ(金)]   (URLバー風)
  //  打数: X / Par Y (残り最短 N 記事)            (副情報)
  //  History: A > B > ...                         (パンくずリスト)
  // =========================================================
  constexpr float kHudX = 14.0f;
  constexpr float kHudY = 14.0f;

  // タブアイコン
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"\U0001f310"; // 🌐
    t.x = kHudX;
    t.y = kHudY + 2.0f;
    t.width = 32.0f;
    t.height = 30.0f;
    t.style = graphics::TextStyle::Guide();
    t.style.fontSize = 20.0f;
    t.style.color = {0.220f, 0.745f, 0.973f, 1.0f};
    t.style.align = graphics::TextAlign::Center;
    // タブアイコンの背景 = URLバー全体の背景をここで描画
    t.style.bgColor = {0.059f, 0.090f, 0.165f, 0.88f};
    t.style.cornerRadius = 10.0f;
    t.style.borderWidth = 1.0f;
    t.style.borderColor = {0.220f, 0.380f, 0.600f, 0.4f};
    t.visible = true;
    t.layer = 10;
    state.browserTabIconEntity = e;
  }

  // 現在ページ名（URLバー風 白テキスト）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"Loading...";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 2.0f;
    t.width = 500.0f;
    t.height = 30.0f;
    t.style = graphics::TextStyle::BrowserURL();
    t.style.bgColor = {0.0f, 0.0f, 0.0f, 0.0f}; // 背景はタブアイコン側で描画
    t.style.borderWidth = 0.0f;
    t.visible = true;
    t.layer = 11;
    state.browserCurrentPageEntity = e;
    // 後方互換: headerEntityも同一エンティティ
    state.headerEntity = e;
  }

  // 矢印セパレーター + ターゲットページ名（金色）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"→ 目標ページ...";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 32.0f;
    t.width = 600.0f;
    t.height = 28.0f;
    t.style = graphics::TextStyle::GoalHighlight();
    t.style.fontSize = 18.0f;
    t.visible = true;
    t.layer = 11;
    state.browserTargetEntity = e;
  }

  // 打数/Par + 最短距離（副情報テキスト）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"打数: 0 / Par ?";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 58.0f;
    t.width = 600.0f;
    t.height = 24.0f;
    t.style = graphics::TextStyle::BrowserSub();
    t.visible = true;
    t.layer = 11;
    state.browserShotInfoEntity = e;
    // 後方互換: shotCountEntityも同一エンティティ
    state.shotCountEntity = e;
  }

  // 経路ブレッドクラム（履歴）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 80.0f;
    t.width = 700.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::BrowserSub();
    t.style.fontSize = 14.0f;
    t.style.color = {0.569f, 0.639f, 0.729f, 0.9f}; // より薄い
    t.visible = true;
    t.layer = 11;
    state.browserHistoryEntity = e;
    // 後方互換: pathEntityも同一エンティティ
    state.pathEntity = e;
  }

  // 後方互換: infoEntityは空エンティティ（古いコードの参照が残るため）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.visible = false;
    state.infoEntity = e;
  }

  // =========================================================
  // 提案5: ショットパネル (画面下中央)
  // 構成: POWER [████░░░] XX% | ACCURACY [●] | CLUB: Driver
  // 位置: x=300, y=620, 幅680
  // =========================================================
  constexpr float kPanelX = 300.0f;
  constexpr float kPanelY = 622.0f;
  constexpr float kPanelW = 680.0f;

  // POWER ラベル
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"POWER";
    t.x = kPanelX;
    t.y = kPanelY;
    t.width = 100.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelLabel();
    // パネル背景をここで描画
    t.style.bgColor = {0.059f, 0.090f, 0.165f, 0.82f};
    t.style.cornerRadius = 12.0f;
    t.style.borderWidth = 1.0f;
    t.style.borderColor = {0.220f, 0.380f, 0.600f, 0.4f};
    t.visible = false; // ショット時のみ表示
    t.layer = 50;
    state.shotPanelPowerLabelEntity = e;
  }

  // POWER値（パーセント表示）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"0%";
    t.x = kPanelX + kPanelW - 70.0f;
    t.y = kPanelY;
    t.width = 65.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelValue();
    t.visible = false;
    t.layer = 51;
    state.shotPanelPowerValueEntity = e;
  }

  // ACCURACY ラベル
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"ACCURACY";
    t.x = kPanelX;
    t.y = kPanelY + 50.0f;
    t.width = 120.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelLabel();
    t.visible = false;
    t.layer = 50;
    state.shotPanelAccuracyLabelEntity = e;
  }

  // ACCURACY値（評価テキスト）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"---";
    t.x = kPanelX + kPanelW - 160.0f;
    t.y = kPanelY + 50.0f;
    t.width = 155.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelValue();
    t.visible = false;
    t.layer = 51;
    state.shotPanelAccuracyValueEntity = e;
  }

  // CLUB表示（クラブ名）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"CLUB: Driver";
    t.x = kPanelX;
    t.y = kPanelY + 95.0f;
    t.width = kPanelW;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ClubName();
    t.style.align = graphics::TextAlign::Center;
    t.style.fontSize = 14.0f;
    t.style.color = {0.569f, 0.639f, 0.729f, 0.9f};
    t.visible = false;
    t.layer = 50;
    state.shotPanelClubLabelEntity = e;
  }

  // --- パワーゲージ (D2D UIBarGauge) - 提案5準拠の大型中央配置 ---
  state.gaugeBarEntity = CreateEntity(ctx.world);
  auto &gauge = ctx.world.Add<UIBarGauge>(state.gaugeBarEntity);
  gauge.x = kPanelX;
  gauge.y = kPanelY + 22.0f;
  gauge.width = kPanelW;
  gauge.height = 24.0f;

  gauge.value = 0.0f;
  gauge.maxValue = 1.0f;
  gauge.color = {0.133f, 0.773f, 0.369f, 1.0f}; // #22C55E 成功色デフォルト
  gauge.bgColor = {0.020f, 0.039f, 0.090f, 0.9f}; // 濃紺背景
  gauge.borderColor = {0.220f, 0.380f, 0.600f, 0.6f};
  gauge.borderWidth = 1.5f;
  gauge.isVisible = false;

  gauge.showImpactZones = false;
  gauge.impactCenter = 0.5f;
  gauge.impactWidthGreat = 0.10f; // Great幅を少し広く
  gauge.impactWidthNice = 0.28f;  // Nice幅
  gauge.showMarker = false;

  // ACCURACY ゲージ行（インパクトゲージ）
  // 既存gaugeBarEntityをフェーズで切り替えして使用するため単一ゲージを維持

  state.gaugeFillEntity = 0;
  state.gaugeMarkerEntity = 0;

  // =========================================================
  // 判定結果 (中央に大きく)
  // =========================================================
  auto judgeE = CreateEntity(ctx.world);
  auto &ji = ctx.world.Add<UIImage>(judgeE);
  ji = UIImage::Create("ui_judge_great.png", 540.0f, 280.0f);
  ji.width = 200.0f;
  ji.height = 80.0f;
  ji.visible = false;
  state.judgeEntity = judgeE;

  // =========================================================
  // ガイドUI (下部)
  // =========================================================
  auto guideE = CreateEntity(ctx.world);
  auto &gt = ctx.world.Add<UIText>(guideE);
  gt.text = L"";
  gt.x = 200.0f;
  gt.y = 688.0f;
  gt.width = 880.0f;
  gt.style = graphics::TextStyle::Guide();
  gt.style.fontSize = 20.0f;
  gt.style.color = {0.792f, 0.835f, 0.886f, 0.9f};
  gt.visible = true;
  gt.layer = 100;
  state.guideEntity = guideE;

  // 後方互換: guideBgEntityは空
  {
    auto e = CreateEntity(ctx.world);
    auto &im = ctx.world.Add<UIImage>(e);
    im.visible = false;
    state.guideBgEntity = e;
  }

  // =========================================================
  // マップビュー強化UI
  // =========================================================

  // ズームインジケーター背景
  m_mapZoomIndicatorBg = CreateEntity(ctx.world);
  auto &zoomBg = ctx.world.Add<UIImage>(m_mapZoomIndicatorBg);
  zoomBg = UIImage::Create("", 1040.0f, 680.0f);
  zoomBg.width = 100.0f;
  zoomBg.height = 20.0f;
  zoomBg.alpha = 0.7f;
  zoomBg.visible = false;
  zoomBg.layer = 105;

  m_mapZoomIndicatorText = CreateEntity(ctx.world);
  auto &zoomTxt = ctx.world.Add<UIText>(m_mapZoomIndicatorText);
  zoomTxt.text = L"100%";
  zoomTxt.x = 1065.0f;
  zoomTxt.y = 683.0f;
  zoomTxt.style = graphics::TextStyle::BrowserSub();
  zoomTxt.style.fontSize = 14.0f;
  zoomTxt.visible = false;
  zoomTxt.layer = 106;

  m_mapCoordText = CreateEntity(ctx.world);
  auto &coordTxt = ctx.world.Add<UIText>(m_mapCoordText);
  coordTxt.text = L"";
  coordTxt.x = 1050.0f;
  coordTxt.y = 205.0f;
  coordTxt.style = graphics::TextStyle::BrowserSub();
  coordTxt.style.fontSize = 13.0f;
  coordTxt.visible = false;
  coordTxt.layer = 102;

  m_mapDistanceText = CreateEntity(ctx.world);
  auto &distTxt = ctx.world.Add<UIText>(m_mapDistanceText);
  distTxt.text = L"";
  distTxt.x = 1050.0f;
  distTxt.y = 220.0f;
  distTxt.style = graphics::TextStyle::BrowserSub();
  distTxt.style.fontSize = 13.0f;
  distTxt.style.color = {0.133f, 0.773f, 0.369f, 0.9f}; // 成功色
  distTxt.visible = false;
  distTxt.layer = 102;

  // ヘルプパネル
  m_mapHelpPanelBg = CreateEntity(ctx.world);
  auto &helpBg = ctx.world.Add<UIImage>(m_mapHelpPanelBg);
  helpBg = UIImage::Create("", 870.0f, 240.0f);
  helpBg.width = 360.0f;
  helpBg.height = 340.0f;
  helpBg.alpha = 0.85f;
  helpBg.visible = false;
  helpBg.layer = 200;

  m_mapHelpTitle = CreateEntity(ctx.world);
  auto &helpTitle = ctx.world.Add<UIText>(m_mapHelpTitle);
  helpTitle.text = L"マップ操作";
  helpTitle.x = 890.0f;
  helpTitle.y = 260.0f;
  helpTitle.style = graphics::TextStyle::CardValue();
  helpTitle.style.fontSize = 28.0f;
  helpTitle.visible = false;
  helpTitle.layer = 201;

  const wchar_t *helpTexts[] = {L"左/右ドラッグ : マップ移動",
                                L"ホイール / +/- : ズーム",
                                L"WASD / 矢印 : マップ移動",
                                L"Space / C : ボール中心",
                                L"F : 全体表示",
                                L"0 : ズームリセット",
                                L"ESC / M : マップ終了",
                                L"? : ヘルプ表示/非表示"};
