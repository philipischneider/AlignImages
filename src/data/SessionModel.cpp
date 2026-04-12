#include "data/SessionModel.h"

#include <algorithm>
#include <string>

namespace align
{
SessionModel CreateDefaultSession()
{
    SessionModel session;
    session.stackA.id = "stack_a";
    session.stackA.name = "Reference Stack";
    session.stackA.modality = "photo";
    session.stackB.id = "stack_b";
    session.stackB.name = "Moving Stack";
    session.stackB.modality = "ct";
    session.pairing.fixedStackId = session.stackA.id;
    session.pairing.movingStackId = session.stackB.id;
    session.projectPreferences.registrationPreset = "ct_photo_initial";
    session.projectPreferences.autoAlignmentMethod = "mask_centroid_orientation";
    session.projectPreferences.maskMethod = "modality_specific_body_mask";
    session.projectPreferences.scoreMethod = "mask_overlap_plus_gradient";
    session.projectPreferences.refinementMethod = "local_similarity_search";

    return session;
}

void RebuildPairs(SessionModel& session)
{
    session.pairing.fixedStackId = session.stackA.id;
    session.pairing.movingStackId = session.stackB.id;
    session.pairing.globalOffset = session.pairing.fixedTimelineOffset - session.pairing.movingTimelineOffset;
    session.pairing.pairs.clear();

    const int fixedCount = static_cast<int>(session.stackA.slices.size());
    const int movingCount = static_cast<int>(session.stackB.slices.size());
    for (int fixedIndex = 0; fixedIndex < fixedCount; ++fixedIndex)
    {
        const int movingIndex = fixedIndex + session.pairing.globalOffset;

        PairRecord pair;
        pair.fixedIndex = fixedIndex;
        pair.movingIndex = movingIndex;
        pair.valid = movingIndex >= 0 && movingIndex < movingCount;
        pair.status = pair.valid ? PairStatus::Candidate : PairStatus::Unmatched;
        session.pairing.pairs.push_back(pair);
    }

    const int maxA = (std::max)(0, fixedCount - 1);
    const int maxB = (std::max)(0, movingCount - 1);
    session.projectPreferences.activeSliceA = (std::clamp)(session.projectPreferences.activeSliceA, 0, maxA);
    session.projectPreferences.activeSliceB = (std::clamp)(session.projectPreferences.activeSliceB, 0, maxB);

    if (!session.pairing.pairs.empty() && session.projectPreferences.activeSliceA < fixedCount)
    {
        const PairRecord& pair = session.pairing.pairs[session.projectPreferences.activeSliceA];
        if (pair.valid)
        {
            session.projectPreferences.activeSliceB = pair.movingIndex;
        }
    }
}
} // namespace align
