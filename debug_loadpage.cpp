void WikiGolfScene::LoadPage(core::GameContext &ctx,
                             const std::string &pageName) {
  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    LOG_ERROR("WikiGolf", "LoadPage: GameState not found!");
    return;
  }

  LOG_INFO("WikiGolf", "Loading page: {}", pageName);

  // 1. 古いホールを削除
  // Queryを使って削除リストを作成（イテレーション中の削除は危険なため）
  std::vector<ecs::Entity> holesToDelete;
  std::vector<ecs::Entity> relatedToDelete; // ラベル・光柱等
  ctx.world.Query<game::components::GolfHole>().Each(
      [&](ecs::Entity e, game::components::GolfHole &hole) {
        holesToDelete.push_back(e);
        // ラベルエンティティも削除対象に
        if (hole.labelEntity != 0) {
          relatedToDelete.push_back(ecs::Entity(hole.labelEntity));
        }
        // 光柱エンティティも削除対象に
        if (hole.pillarEntity != 0) {
          relatedToDelete.push_back(ecs::Entity(hole.pillarEntity));
        }
      });
  for (auto e : relatedToDelete) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }
  for (auto e : holesToDelete) {
    ctx.world.DestroyEntity(e);
  }
  std::vector<ecs::Entity> flagsToDelete;
  ctx.world.Query<game::components::HoleFlag>().Each(
      [&](ecs::Entity e, game::components::HoleFlag &) {
        flagsToDelete.push_back(e);
      });
  for (auto e : flagsToDelete) {
    ctx.world.DestroyEntity(e);
  }
  state->holes.clear();
  LOG_DEBUG("WikiGolf", "LoadPage (after delete holes): Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  // 2. 記事データ取得
  game::systems::WikiClient wikiClient;
  std::vector<game::WikiLink> allLinks;
  std::string articleText;

  // キャッシュは初回かつページ名が一致する場合のみ使用可能にする（簡易チェック）
  // ただし初回LoadPage以外でm_hasPreloadedDataがtrueになることはほぼない
  if (m_hasPreloadedData) {
    LOG_INFO("WikiGolf", "Using preloaded links and text for {}", pageName);
    allLinks = std::move(m_preloadedLinks);
    articleText = std::move(m_preloadedExtract);
    m_hasPreloadedData = false; // 使い終わったらフラグを下ろす
  } else {
    LOG_INFO("WikiGolf", "Fetching live data for {}", pageName);
    // リンク取得（多めに取得してフィルタリング）
    allLinks = wikiClient.FetchPageLinks(pageName, 0);
    // 記事テキスト取得
    articleText = wikiClient.FetchPageExtract(pageName, 5000);
  }

  // 3. リンクのフィルタリング
  std::vector<std::pair<std::string, std::wstring>> validLinks;

  // フィルタリング（年・月・日・数値のみを除外）
  auto isIgnored = [](const std::string &t) {
    if (t.empty())
      return true;
    // 末尾チェック (UTF-8)
    if (t.size() >= 3) {
      std::string suffix = t.substr(t.size() - 3);
      if (suffix == "年" || suffix == "月" || suffix == "日")
        return true;
    }
    // 数値のみ
    if (std::all_of(t.begin(), t.end(),
                    [](unsigned char c) { return std::isdigit(c); }))
      return true;
    return false;
  };

  // リンク数の動的上限を計算（本文2000文字につき5リンク、固定上限50）
  const size_t kLinksPerChars = 5;
  const size_t kCharsPerUnit = 2000;
  const size_t kMaxLinks = 50;
  size_t dynamicLimit =
      (articleText.length() / kCharsPerUnit + 1) * kLinksPerChars;
  size_t linkLimit = (dynamicLimit < kMaxLinks) ? dynamicLimit : kMaxLinks;

  for (const auto &link : allLinks) {
    if (isIgnored(link.title))
      continue;

    // ターゲットページは別枠で処理するのでスキップ
    if (link.title == state->targetPage)
      continue;

    // 本文に含まれているかチェック
    if (articleText.find(link.title) != std::string::npos) {
      validLinks.push_back({link.title, core::ToWString(link.title)});
    }

    if (validLinks.size() >= linkLimit)
      break;
  }

  // ターゲットページは本文に含まれていれば無条件追加
  if (!state->targetPage.empty() &&
      articleText.find(state->targetPage) != std::string::npos) {
    bool targetExists = false;
    for (const auto &v : validLinks) {
      if (v.first == state->targetPage) {
        targetExists = true;
        break;
      }
    }
    if (!targetExists) {
      validLinks.push_back(
          {state->targetPage, core::ToWString(state->targetPage)});
      LOG_INFO("WikiGolf", "Target page '{}' added (in article)",
               state->targetPage);
    }
  }

  // リンク不足時の補充
  if (validLinks.size() < 3) {
    for (const auto &link : allLinks) {
      bool exists = false;
      for (const auto &v : validLinks)
        if (v.first == link.title)
          exists = true;
      if (!exists && !isIgnored(link.title)) {
        validLinks.push_back({link.title, core::ToWString(link.title)});
        if (validLinks.size() >= 5)
          break;
      }
    }
  }

  // ラムダ式内で使うisIgnoredをここでも定義する必要があったので、
  // 上記の補充ループ内のisIgnoredはコンパイルエラーになる可能性がある。
  // まじめに実装しなおす。

  // 4. フィールドサイズ計算 (フレキシブル化)
  const float minFieldWidth = 20.0f * kFieldScale;
  const float minFieldDepth = 30.0f * kFieldScale;

  // 記事の長さを基準にする (1500文字を1ユニット程度と想定)
  float articleLengthFactor = (float)articleText.length() / 1500.0f;
  if (articleLengthFactor < 1.0f)
    articleLengthFactor = 1.0f;

  // 横幅も適度にスケールさせつつ、縦長になりすぎないよう抑制する
  float fieldWidth = minFieldWidth * std::pow(articleLengthFactor, 0.45f);
  // 高さ（奥行き）は後で逆算するため、ここでは最小値を設定
  float fieldDepth = minFieldDepth;

  // 安全策: 最小サイズ保証 + 幅の上限
  fieldWidth = std::clamp(fieldWidth, minFieldWidth, minFieldWidth * 4.0f);
  fieldDepth = std::max(fieldDepth, minFieldDepth);

  // 5. テクスチャ生成
  // 幅は16384制限があるが、高さはタイリングで無限に対応可能なので制限しない
  const uint32_t kMaxTexWidth = 16384;
  float texScale = 1.0f;

  uint32_t texWidth = static_cast<uint32_t>(fieldWidth * 100.0f);
  uint32_t texHeight = static_cast<uint32_t>(fieldDepth * 100.0f);

  if (texWidth > kMaxTexWidth) {
    texScale = (float)kMaxTexWidth / (float)texWidth;
    texWidth = kMaxTexWidth;
    texHeight =
        (uint32_t)(texHeight *
                   texScale); // アスペクト比維持で高さ解像度も一応下げる

    LOG_INFO("WikiGolf", "Width capped to {}. Scale: {:.2f}", kMaxTexWidth,
             texScale);
  }

  std::vector<std::pair<std::wstring, std::string>> linkPairs;
  for (const auto &link : validLinks) {
    linkPairs.push_back({link.second, link.first});
  }

  // GenerateTexture呼び出し
  auto texResult = m_textureGenerator->GenerateTexture(
      core::ToWString(pageName), core::ToWString(articleText), linkPairs,
      state->targetPage, texWidth, texHeight);

  // 実態に合わせてフィールドサイズを計算（1m = 100px基準）
  float actualFieldDepth = (float)texResult.height / (100.0f * texScale);
  float actualFieldWidth = (float)texResult.width / (100.0f * texScale);

  // アスペクト比を維持しつつ、最小サイズ(minFieldWidth/Depth)を満たすようにスケーリング
  // 独立して max() をかけるとアスペクト比が崩れて文字が歪むため
  float scaleFix = 1.0f;
  if (actualFieldWidth < minFieldWidth) {
    scaleFix = std::max(scaleFix, minFieldWidth / actualFieldWidth);
  }
  if (actualFieldDepth < minFieldDepth) {
    scaleFix = std::max(scaleFix, minFieldDepth / actualFieldDepth);
  }

  fieldWidth = actualFieldWidth * scaleFix;
  fieldDepth = actualFieldDepth * scaleFix;

  m_wikiTexture =
      std::make_unique<graphics::WikiTextureResult>(std::move(texResult));

  // スカイボックスをページテーマに応じて動的ロード
  auto *skyboxComp = ctx.world.Get<components::Skybox>(m_skyboxEntity);
  if (skyboxComp && m_skyboxGenerator) {
    graphics::SkyboxTheme theme =
        m_skyboxGenerator->DetermineTheme(pageName, articleText);
    std::wstring themeName =
        graphics::SkyboxTextureGenerator::GetThemeFileName(theme);
    std::wstring skyboxBasePath =
        L"Assets/textures/runtime_skybox/skybox_" + themeName;

    if (m_skyboxGenerator->LoadCubemapFromFiles(
            ctx.graphics.GetDevice(), skyboxBasePath, skyboxComp->cubemapSRV)) {
      LOG_INFO("WikiGolf", "Skybox loaded for theme: {}, SRV valid: {}",
               core::ToString(themeName),
               skyboxComp->cubemapSRV ? "yes" : "no");
      skyboxComp->isVisible = true;

      // === 環境設定の適用 ===
      m_currentSkyboxTheme = theme;
      auto preset = game::components::GetEnvironmentPreset(theme);

      // パーティクル設定
      auto particleConfig =
          game::systems::GetParticleConfig(preset.particlePreset);
      m_particleSystem.Configure(particleConfig);

      // 環境状態を反映（TimeOfDayシステムなどへ）
      m_timeOfDay.SetTime(preset.timeOfDay); // テーマに応じた初期時間

      // 環境音切り替え（AudioSystemが必要）
      // TODO: AudioSystem連携

      // ポストプロセス初期設定（霧など）
      m_postProcess.SetFog(preset.fogColor, preset.fogDensity, preset.fogStart,
                           preset.fogEnd);
      m_postProcess.SetColorGrading(preset.colorTint, preset.brightness,
                                    preset.saturation, preset.contrast);

    } else {
      // Fallback to Default
      std::wstring defaultPath =
          L"Assets/textures/runtime_skybox/skybox_Default";
      if (m_skyboxGenerator->LoadCubemapFromFiles(
              ctx.graphics.GetDevice(), defaultPath, skyboxComp->cubemapSRV)) {
        LOG_INFO("WikiGolf", "Skybox fallback to Default");
        skyboxComp->isVisible = true;
      } else {
        LOG_WARN("WikiGolf", "Failed to load any skybox");
        skyboxComp->isVisible = false;
      }
    }
  }

  // 異常な巨大値を防止しつつ、超長文でも収まるよう高めの上限を設定
  const float kMaxSafeDepth = 20000.0f; // 20km相当
  const float kMaxSafeWidth = 20000.0f;

  fieldDepth = std::min(fieldDepth, kMaxSafeDepth);
  fieldWidth = std::min(fieldWidth, kMaxSafeWidth);

  LOG_INFO("WikiGolf", "Final field size: {}x{}", fieldWidth, fieldDepth);

  // 再計上したフィールド寸法を保存
  m_fieldWidth = fieldWidth;
  m_fieldDepth = fieldDepth;
  state->fieldWidth = fieldWidth;
  state->fieldDepth = fieldDepth;
  float fieldExtent = std::max(m_fieldWidth, m_fieldDepth);
  m_maxMapZoom = game::utils::CalculateMaxMapZoom(fieldExtent, kMinMapViewSpan,
                                                  m_baseMaxMapZoom);
  m_mapZoom = game::utils::ClampMapZoom(m_mapZoom, m_minMapZoom, m_maxMapZoom);
  m_targetMapZoom =
      game::utils::ClampMapZoom(m_targetMapZoom, m_minMapZoom, m_maxMapZoom);

  // カメラの描画距離（farZ）をフィールド奥行きに合わせて拡張
  auto *cam = ctx.world.Get<components::Camera>(m_cameraEntity);
  if (cam) {
    cam->farZ = std::max(1000.0f, fieldDepth * 2.5f);
  }

  // 6. 地形（フィールド）再構築
  LOG_DEBUG("WikiGolf", "Building field size: {}x{}", fieldWidth, fieldDepth);
  if (m_terrainSystem) {
    m_terrainSystem->BuildField(ctx, pageName, *m_wikiTexture, fieldWidth,
                                fieldDepth);
    m_floorEntity = m_terrainSystem->GetFloorEntity(); // カメラ追従などに必要
  }

  // 6.5 ボール位置をフィールドサイズに合わせて再配置
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  auto *ballRB = ctx.world.Get<RigidBody>(m_ballEntity);
  if (ballT) {
    // フィールド手前（-Z方向）の80%地点、中央X、床より少し上
    ballT->position = {0.0f, 1.0f, -fieldDepth * 0.4f};
    LOG_DEBUG("WikiGolf", "Ball repositioned to: ({}, {}, {})",
              ballT->position.x, ballT->position.y, ballT->position.z);
    if (ballRB) {
      ballRB->velocity = {0.0f, 0.0f, 0.0f}; // 速度リセット
    }
    SyncMapCenterToBall(ctx, 0.0f, true);
  } else {
    LOG_ERROR("WikiGolf", "Ball transform not found!");
  }

  // 7. ホール配置
  // 同じ座標に複数のホールを作らないよう追跡（座標ベース）
  std::vector<std::pair<float, float>> createdHolePositions;
  const float texWidthF = (float)m_wikiTexture->width;
  const float texHeightF = (float)m_wikiTexture->height;
  const float minHoleDistance = 0.2f; // ホール間の最小距離

  LOG_INFO("WikiGolf",
           "Hole placement: texture links count = {}, validLinks = {}, "
           "fieldSize = {}x{}",
           m_wikiTexture->links.size(), validLinks.size(), fieldWidth,
           fieldDepth);

  for (const auto &linkRegion : m_wikiTexture->links) {
    float texCenterX = linkRegion.x + linkRegion.width * 0.5f;
    float texCenterY = linkRegion.y + linkRegion.height * 0.5f;
    float worldX = (texCenterX / texWidthF - 0.5f) * fieldWidth;
    float worldZ = (0.5f - texCenterY / texHeightF) * fieldDepth;

    // 既に近い位置にホールがあるかチェック
    bool tooClose = false;
    for (const auto &pos : createdHolePositions) {
      float dx = worldX - pos.first;
      float dz = worldZ - pos.second;
      if (dx * dx + dz * dz < minHoleDistance * minHoleDistance) {
        tooClose = true;
        break;
      }
    }
    if (tooClose) {
      continue;
    }
    createdHolePositions.push_back({worldX, worldZ});

    // SDOW距離計算
    int hops = -1;
    if (m_shortestPath && m_shortestPath->IsAvailable() &&
        state->targetPageId != -1) {
      auto result = m_shortestPath->FindShortestPath(linkRegion.targetPage,
                                                     state->targetPageId, 6);
      if (result.success) {
        hops = result.degrees;
      }
    }

    LOG_DEBUG("WikiGolf",
              "Creating hole at ({}, {}) for '{}', isTarget={}, hops={}",
              worldX, worldZ, linkRegion.targetPage, linkRegion.isTarget, hops);

    CreateHole(ctx, worldX, worldZ, linkRegion.targetPage, linkRegion.isTarget,
               hops);
  }

  LOG_INFO("WikiGolf", "Total holes created: {}", createdHolePositions.size());

  // 8. 風設定
  float windSpeed = 0.0f;
  if (articleText.length() > 2000) {
    windSpeed = 3.0f + (float)(rand() % 20) / 10.0f;
  } else if (articleText.length() > 500) {
    windSpeed = 1.0f + (float)(rand() % 20) / 10.0f;
  }
  float windAngle = (float)(rand() % 360) * 3.14159f / 180.0f;
  DirectX::XMFLOAT2 windDir = {cosf(windAngle), sinf(windAngle)};

  state->windSpeed = windSpeed;
  state->windDirection = windDir;

  // --- 提案2: 風カード更新 ---
  // 8方向矢印マッピング: 右→右上→上→左上→左→左下→下→右下
  int dir8 = (int)((windAngle + 3.14159f / 8.0f) / (3.14159f / 4.0f)) % 8;
  const wchar_t *arrowsCard[] = {L"→", L"↗", L"↑", L"↖", L"←", L"↙", L"↓", L"↘"};

  // 風速数値テキスト
  if (auto *wv = ctx.world.Get<UIText>(state->windCardValueEntity)) {
    wchar_t buf[32];
    swprintf_s(buf, 32, L"%.1f m/s", windSpeed);
    wv->text = buf;
  }
  // 方向矢印（スカイブルー）
  if (auto *wu = ctx.world.Get<UIText>(state->windCardUnitEntity)) {
    wu->text = arrowsCard[dir8];
  }

  // 9. ブラウザ風HUD更新
  // 現在ページ名（URLバー風）
  if (auto *cp = ctx.world.Get<UIText>(state->browserCurrentPageEntity)) {
    cp->text = core::ToWString(pageName);
  }
  // 後方互換 headerEntity (同一エンティティのため不要だが安全のため)

  // ターゲットページ名（金色 + 矢印）
  if (auto *tp = ctx.world.Get<UIText>(state->browserTargetEntity)) {
    tp->text = L"→  " + core::ToWString(state->targetPage);
  }

  state->currentPage = pageName;
  state->pathHistory.push_back(pageName);

  // 経路ブレッドクラム
  if (auto *ph = ctx.world.Get<UIText>(state->browserHistoryEntity)) {
    // 最大5件 (超過時は先頭省略)
    const auto &hist = state->pathHistory;
    int startIdx = (int)hist.size() > 5 ? (int)hist.size() - 5 : 0;
    std::wstring histText = (startIdx > 0) ? L"... > " : L"";
    for (int i = startIdx; i < (int)hist.size(); ++i) {
      if (i > startIdx)
        histText += L" > ";
      histText += core::ToWString(hist[i]);
    }
    ph->text = histText;
  }

  // Par計算
  int calculatedPar = -1;
  if (m_shortestPath) {
    game::systems::ShortestPathResult result;
    if (state->targetPageId != -1) {
      result =
          m_shortestPath->FindShortestPath(pageName, state->targetPageId, 20);
    } else {
      result =
          m_shortestPath->FindShortestPath(pageName, state->targetPage, 20);
    }
    if (result.success)
      calculatedPar = result.degrees;
  }
  m_calculatedPar = calculatedPar;

  int par =
      (calculatedPar > 0) ? calculatedPar : (int)validLinks.size() / 2 + 2;
  state->par = par;

  // 打数/Par + 最短距離情報
  std::wstring distInfo = L"";
  if (calculatedPar > 0) {
    distInfo = L"  ·  残り最短 " + std::to_wstring(calculatedPar) + L" 記事";
    LOG_INFO("WikiGolf", "Path found! Degrees: {}", calculatedPar);
  } else {
    LOG_INFO("WikiGolf", "Path calc failed or fallback used.");
  }

  if (auto *si = ctx.world.Get<UIText>(state->browserShotInfoEntity)) {
    si->text = L"打数: " + std::to_wstring(state->shotCount) + L" / Par " +
               std::to_wstring(state->par) + distInfo;
    LOG_INFO("WikiGolf", "Updated HUD text: {}", core::ToString(si->text));
  }

  // クラブパネルのクラブ名更新
  if (auto *cl = ctx.world.Get<UIText>(state->shotPanelClubLabelEntity)) {
    cl->text = L"CLUB: " + core::ToWString(m_currentClub.name);
  }
}