#pragma once
/**
 * @file TutorialStep.h
 * @brief チュートリアルの進行段階
*/

namespace game::controllers {

/** @brief チュートリアルの進行段階（宣言順に進む）*/
enum class TutorialStep {
    Intro,
    Camera,
    Club,
    Aim,
    Power,
    Impact,
    TerrainInfo,
    FlagInfo,
    MapOpen,
    MapPan,
    MapZoom,
    MapAim,
    MapHelp,
    MapClose,
    LinkCup,
    GoalCup,
    Done
};

} // namespace game::controllers
