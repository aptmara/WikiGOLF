#pragma once
/**
 * @file BuildEdition.h
 * @brief ビルド種別 (製品版 / 体験版) の判定
 * @details CMake オプション WIKIGOLF_TRIAL_BUILD=ON でビルドすると体験版になる。
 *          体験版ではタイトルからチュートリアルのみ開始でき、
 *          チュートリアル完了後に始まる1プレイを終えるとタイトルへ戻る。
*/

namespace core {

#if defined(WIKIGOLF_TRIAL_BUILD) && WIKIGOLF_TRIAL_BUILD
inline constexpr bool kTrialBuild = true;
#else
inline constexpr bool kTrialBuild = false;
#endif

} // namespace core
