#include "WikiPageLoader.h"

#include "../../core/StringUtils.h"
#include "../components/WikiComponents.h"
#include "../utils/GameplayPhysicsConstants.h"

namespace game::scenes {

void WikiPageLoader::CaptureCourseIntroductionData(
    const std::string& pageName, const std::string& articleText,
    const game::components::GolfGameState& state) {
  CourseIntroductionData data;
  data.pageName = pageName;
  data.abstractText = game::utils::FormatCourseAbstract(
      core::ToWString(articleText));
  data.fieldWidth = state.fieldWidth;
  data.fieldDepth = state.fieldDepth;
  data.windSpeed = state.windSpeed;
  data.par = state.par;
  data.holes.reserve(m_buildHoleCandidates.size());

  for (const HolePlacementCandidate& candidate : m_buildHoleCandidates) {
    game::utils::CourseIntroductionHole hole;
    hole.x = candidate.x;
    if (m_terrainSystem) {
      hole.y = game::physics::ToVisualSurfaceHeight(
          m_terrainSystem->GetHeight(candidate.x, candidate.z));
    }
    hole.z = candidate.z;
    hole.linkTarget = candidate.linkTarget;
    hole.isTarget = candidate.isTarget;
    hole.hopsToTarget = candidate.isTarget ? 0 : candidate.hopsToTarget;
    hole.originalIndex = candidate.originalIndex;
    data.holes.push_back(std::move(hole));
  }

  m_courseIntroductionData = std::move(data);
}

} // namespace game::scenes
