#include "WikiGolfHUD.h"
#include "../../ecs/World.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../../core/StringUtils.h"
#include "../utils/UIConstants.h"
#include <algorithm>

namespace game {
namespace controllers {

void WikiGolfHUD::Initialize(core::GameContext& ctx) {
    // ---------------------------------------------------------
    // Browser-style HUD (top-left)
    // ---------------------------------------------------------
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"WEB";
        t.x = game::ui::kBrowserHudX;
        t.y = game::ui::kBrowserHudY + 2.0f;
        t.width = game::ui::kBrowserIconWidth;
        t.height = 30.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = game::ui::kBrowserFontSize;
        t.style.color = game::ui::kColorAccent;
        t.style.align = graphics::TextAlign::Center;
        t.style.bgColor = game::ui::kColorBgDark;
        t.style.cornerRadius = 10.0f;
        t.style.borderWidth = 1.0f;
        t.style.borderColor = game::ui::kColorBorder;
        t.visible = true;
        t.layer = game::ui::kLayerBrowser;
        m_ui.browserTabIconEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"Loading...";
        t.x = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y = game::ui::kBrowserHudY + 2.0f;
        t.width = game::ui::kBrowserUrlWidth;
        t.height = 30.0f;
        t.style = graphics::TextStyle::BrowserURL();
        t.style.fontSize = game::ui::kBrowserFontSize;
        t.style.bgColor = {0.0f, 0.0f, 0.0f, 0.0f};
        t.style.borderWidth = 0.0f;
        t.visible = true;
        t.layer = game::ui::kLayerBrowser + 1;
        m_ui.browserCurrentPageEntity = e;
        m_ui.headerEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"-> Target page...";
        t.x = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y = game::ui::kBrowserHudY + game::ui::kBrowserLineSpacing + 6.0f;
        t.width = 600.0f;
        t.height = 28.0f;
        t.style = graphics::TextStyle::GoalHighlight();
        t.style.fontSize = game::ui::kBrowserGoalFontSize;
        t.visible = true;
        t.layer = game::ui::kLayerBrowser + 1;
        m_ui.browserTargetEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"Shots: 0 / Par ?";
        t.x = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y = game::ui::kBrowserHudY + game::ui::kBrowserLineSpacing * 2.0f + 6.0f;
        t.width = 600.0f;
        t.height = 24.0f;
        t.style = graphics::TextStyle::BrowserSub();
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.visible = true;
        t.layer = game::ui::kLayerBrowser + 1;
        m_ui.browserShotInfoEntity = e;
        m_ui.shotCountEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"";
        t.x = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y = game::ui::kBrowserHudY + game::ui::kBrowserLineSpacing * 3.0f + 2.0f;
        t.width = 700.0f;
        t.height = 22.0f;
        t.style = graphics::TextStyle::BrowserSub();
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.style.color = game::ui::kColorTextSub;
        t.visible = true;
        t.layer = game::ui::kLayerBrowser + 1;
        m_ui.browserHistoryEntity = e;
        m_ui.pathEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.visible = false;
        m_ui.infoEntity = e;
    }

    // ---------------------------------------------------------
    // Wind card (top-right)
    // ---------------------------------------------------------
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"WIND";
        t.x = game::ui::kWindCardX + 10.0f;
        t.y = game::ui::kWindCardY + 8.0f;
        t.width = game::ui::kWindCardWidth - 20.0f;
        t.height = 20.0f;
        t.style = graphics::TextStyle::CardLabel();
        t.style.fontSize = game::ui::kWindLabelFontSize;
        t.style.bgColor = game::ui::kColorBgDark;
        t.style.cornerRadius = 10.0f;
        t.style.borderWidth = 1.0f;
        t.style.borderColor = game::ui::kColorBorder;
        t.visible = true;
        t.layer = game::ui::kLayerWind;
        m_ui.windCardLabelEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"--";
        t.x = game::ui::kWindCardX + 10.0f;
        t.y = game::ui::kWindCardY + 30.0f;
        t.width = 120.0f;
        t.height = 38.0f;
        t.style = graphics::TextStyle::CardValue();
        t.style.fontSize = game::ui::kWindValueFontSize;
        t.visible = true;
        t.layer = game::ui::kLayerWind + 1;
        m_ui.windCardValueEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"m/s";
        t.x = game::ui::kWindCardX + 130.0f;
        t.y = game::ui::kWindCardY + 30.0f;
        t.width = 80.0f;
        t.height = 38.0f;
        t.style = graphics::TextStyle::CardValue();
        t.style.color = game::ui::kColorAccent;
        t.style.fontSize = game::ui::kWindValueFontSize;
        t.style.align = graphics::TextAlign::Center;
        t.visible = true;
        t.layer = game::ui::kLayerWind + 1;
        m_ui.windCardUnitEntity = e;
    }

    {
        auto windE = ctx.world.CreateEntity();
        auto& wt = ctx.world.Add<game::components::UIText>(windE);
        wt.visible = false;
        m_ui.windEntity = windE;
    }

    {
        auto windArrowE = ctx.world.CreateEntity();
        auto& wa = ctx.world.Add<game::components::UIImage>(windArrowE);
        wa = game::components::UIImage::Create("", 0.0f, 0.0f);
        wa.width = 0.0f;
        wa.height = 0.0f;
        wa.visible = false;
        m_ui.windArrowEntity = windArrowE;
    }

    // ---------------------------------------------------------
    // Shot panel (bottom-center)
    // ---------------------------------------------------------
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"POWER";
        t.x = game::ui::kShotPanelX;
        t.y = game::ui::kShotPanelY;
        t.width = 100.0f;
        t.height = 22.0f;
        t.style = graphics::TextStyle::ShotPanelLabel();
        t.style.fontSize = game::ui::kShotLabelFontSize;
        t.style.bgColor = game::ui::kColorBgPanel;
        t.style.cornerRadius = 12.0f;
        t.style.borderWidth = 1.0f;
        t.style.borderColor = game::ui::kColorBorder;
        t.visible = true;
        t.layer = game::ui::kLayerShotPanel;
        m_ui.shotPanelPowerLabelEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"0%";
        t.x = game::ui::kShotPanelX + game::ui::kShotPanelWidth - 70.0f;
        t.y = game::ui::kShotPanelY;
        t.width = 65.0f;
        t.height = 22.0f;
        t.style = graphics::TextStyle::ShotPanelValue();
        t.style.fontSize = game::ui::kShotValueFontSize;
        t.visible = true;
        t.layer = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelPowerValueEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"ACCURACY";
        t.x = game::ui::kShotPanelX;
        t.y = game::ui::kShotPanelY + 50.0f;
        t.width = 120.0f;
        t.height = 22.0f;
        t.style = graphics::TextStyle::ShotPanelLabel();
        t.style.fontSize = game::ui::kShotLabelFontSize;
        t.visible = true;
        t.layer = game::ui::kLayerShotPanel;
        m_ui.shotPanelAccuracyLabelEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"---";
        t.x = game::ui::kShotPanelX + game::ui::kShotPanelWidth - 160.0f;
        t.y = game::ui::kShotPanelY + 50.0f;
        t.width = 155.0f;
        t.height = 22.0f;
        t.style = graphics::TextStyle::ShotPanelValue();
        t.style.fontSize = game::ui::kShotValueFontSize;
        t.visible = true;
        t.layer = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelAccuracyValueEntity = e;
    }

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"CLUB: Driver";
        t.x = game::ui::kShotPanelX;
        t.y = game::ui::kShotPanelY + 95.0f;
        t.width = game::ui::kShotPanelWidth;
        t.height = 22.0f;
        t.style = graphics::TextStyle::ClubName();
        t.style.align = graphics::TextAlign::Center;
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.style.color = game::ui::kColorTextSub;
        t.visible = true;
        t.layer = game::ui::kLayerShotPanel;
        m_ui.shotPanelClubLabelEntity = e;
    }

    // ---------------------------------------------------------
    // Power Gauge (Using new UIBarGauge)
    // ---------------------------------------------------------
    {
        auto e = ctx.world.CreateEntity();
        auto& gauge = ctx.world.Add<game::components::UIBarGauge>(e);
        gauge.value = 0.0f;
        gauge.maxValue = 1.0f;
        gauge.color = game::ui::kColorWarning; // フィル色
        gauge.bgColor = game::ui::kColorBgDark; // 背景色
        gauge.borderColor = game::ui::kColorBorder;
        gauge.borderWidth = game::ui::kGaugeBorderWidth;
        gauge.x = game::ui::kShotPanelX;
        gauge.y = game::ui::kShotPanelY + 22.0f;
        gauge.width = game::ui::kShotPanelWidth;
        gauge.height = game::ui::kGaugeHeight;
        gauge.isVisible = true;

        gauge.showMarker = true;
        gauge.markerValue = 0.0f;
        gauge.markerColor = game::ui::kColorWhite;

        gauge.showImpactZones = true;
        gauge.impactCenter = 0.5f;
        gauge.impactWidthGreat = game::ui::kImpactWidthGreat;
        gauge.impactWidthNice = game::ui::kImpactWidthNice;

        m_ui.gaugeBarEntity = e;
    }

    // ---------------------------------------------------------
    // Judge text (center)
    // ---------------------------------------------------------
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"";
        t.x = game::ui::kJudgeTextX;
        t.y = game::ui::kJudgeTextY;
        t.width = 200.0f;
        t.height = 80.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = game::ui::kJudgeFontSize;
        t.style.align = graphics::TextAlign::Center;
        t.visible = true;
        t.layer = game::ui::kLayerJudge;
        m_ui.judgeEntity = e;
    }
}

void WikiGolfHUD::Update(core::GameContext& ctx, float dt, const game::components::GolfGameState& state,
                         float currentPower, float confirmedPower,
                         float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                         const std::string& clubName) {
    UpdateGuideUI(ctx, state);
    UpdateWindUI(ctx, windSpeed, windDir, cameraYaw);

    if (m_ui.browserCurrentPageEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.browserCurrentPageEntity)) {
        auto* pt = ctx.world.Get<game::components::UIText>(m_ui.browserCurrentPageEntity);
        pt->text = core::ToWString(state.currentPage);
    }

    if (m_ui.browserTargetEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.browserTargetEntity)) {
        auto* tt = ctx.world.Get<game::components::UIText>(m_ui.browserTargetEntity);
        tt->text = L"-> " + core::ToWString(state.targetPage);
    }

    if (m_ui.browserShotInfoEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.browserShotInfoEntity)) {
        auto* st = ctx.world.Get<game::components::UIText>(m_ui.browserShotInfoEntity);
        st->text = L"Shots: " + std::to_wstring(state.shotCount) + L" / Par " + std::to_wstring(state.par);
    }

    if (m_ui.shotPanelPowerValueEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.shotPanelPowerValueEntity)) {
        auto* pt = ctx.world.Get<game::components::UIText>(m_ui.shotPanelPowerValueEntity);
        if (confirmedPower > 0.0f) {
            pt->text = std::to_wstring((int)(confirmedPower * 100)) + L"%";
            pt->style.color = game::ui::kColorWarning;
        } else {
            pt->text = std::to_wstring((int)(currentPower * 100)) + L"%";
            pt->style.color = game::ui::kColorWhite;
        }
    }

    if (m_ui.shotPanelClubLabelEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.shotPanelClubLabelEntity)) {
        auto* ct = ctx.world.Get<game::components::UIText>(m_ui.shotPanelClubLabelEntity);
        ct->text = core::ToWString(clubName);
    }
}

void WikiGolfHUD::UpdatePowerGauge(core::GameContext& ctx, float fillValue, float markerValue, float minPower, float maxPower) {
    if (m_ui.gaugeBarEntity != UINT32_MAX && ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity)) {
        auto* gauge = ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity);
        float normalizedFillValue = 0.0f;
        float normalizedMarkerValue = 0.0f;
        if (maxPower > minPower && maxPower > 0.0f) {
            normalizedFillValue = (fillValue - minPower) / (maxPower - minPower);
            normalizedMarkerValue = (markerValue - minPower) / (maxPower - minPower);
        }

        gauge->value = std::clamp(normalizedFillValue, 0.0f, 1.0f);
        gauge->markerValue = std::clamp(normalizedMarkerValue, 0.0f, 1.0f);
    }
}

void WikiGolfHUD::UpdateJudge(core::GameContext& ctx, const std::wstring& text, const DirectX::XMFLOAT4& color) {
    if (m_ui.judgeEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.judgeEntity)) {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.judgeEntity);
        t->text = text;
        t->style.color = color;
    }
    if (m_ui.shotPanelAccuracyValueEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity)) {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity);
        t->text = text;
        t->style.color = color;
    }
}

void WikiGolfHUD::UpdateWindUI(core::GameContext& ctx, float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw) {
    if (m_ui.windCardValueEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.windCardValueEntity)) {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.windCardValueEntity);
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1f", windSpeed);
        t->text = buf;
    }

    if (m_ui.windCardUnitEntity != UINT32_MAX && ctx.world.Has<game::components::UIText>(m_ui.windCardUnitEntity)) {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.windCardUnitEntity);
        DirectX::XMVECTOR wdir = DirectX::XMVectorSet(windDir.x, 0, windDir.y, 0);
        DirectX::XMVECTOR camForward = DirectX::XMVectorSet(sin(cameraYaw), 0, cos(cameraYaw), 0);

        DirectX::XMFLOAT3 cross;
        DirectX::XMStoreFloat3(&cross, DirectX::XMVector3Cross(camForward, wdir));
        float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(camForward, wdir));
        float angle = atan2(cross.y, dot);

        std::wstring arrow = L"\u2191";
        if (angle > DirectX::XM_PI/8 && angle <= 3*DirectX::XM_PI/8) arrow = L"\u2197";
        else if (angle > 3*DirectX::XM_PI/8 && angle <= 5*DirectX::XM_PI/8) arrow = L"\u2192";
        else if (angle > 5*DirectX::XM_PI/8 && angle <= 7*DirectX::XM_PI/8) arrow = L"\u2198";
        else if (angle > 7*DirectX::XM_PI/8 || angle <= -7*DirectX::XM_PI/8) arrow = L"\u2193";
        else if (angle < -5*DirectX::XM_PI/8) arrow = L"\u2199";
        else if (angle < -3*DirectX::XM_PI/8) arrow = L"\u2190";
        else if (angle < -DirectX::XM_PI/8) arrow = L"\u2196";

        t->text = arrow + L" m/s";
    }
}


void WikiGolfHUD::UpdateGuideUI(core::GameContext& ctx, const game::components::GolfGameState& state) {
    // Implementation placeholder
}

} // namespace controllers
} // namespace game
