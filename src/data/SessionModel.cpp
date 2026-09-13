#include "data/SessionModel.h"

#include <algorithm>
#include <string>

namespace align
{
SessionModel CreateDefaultSession()
{
    SessionModel session;

    StackModel stackA;
    stackA.id = "stack_a";
    stackA.name = "Reference Stack";
    stackA.modality = "photo";

    StackModel stackB;
    stackB.id = "stack_b";
    stackB.name = "Moving Stack";
    stackB.modality = "ct";

    session.stacks.push_back(stackA);
    session.stacks.push_back(stackB);

    PairingModel pairing;
    pairing.fixedStackId = stackA.id;
    pairing.movingStackId = stackB.id;
    pairing.id = MakePairingId(pairing.fixedStackId, pairing.movingStackId);
    pairing.label = stackA.name + " <-> " + stackB.name;
    session.pairings.push_back(pairing);
    session.activePairingId = pairing.id;

    session.projectPreferences.registrationPreset = "ct_photo_initial";
    session.projectPreferences.autoAlignmentMethod = "mask_centroid_orientation";
    session.projectPreferences.maskMethod = "modality_specific_body_mask";
    session.projectPreferences.scoreMethod = "mask_overlap_plus_gradient";
    session.projectPreferences.refinementMethod = "local_similarity_search";

    return session;
}

StackModel* FindStack(SessionModel& session, const StackId& id)
{
    for (StackModel& stack : session.stacks)
    {
        if (stack.id == id)
        {
            return &stack;
        }
    }
    return nullptr;
}

const StackModel* FindStack(const SessionModel& session, const StackId& id)
{
    for (const StackModel& stack : session.stacks)
    {
        if (stack.id == id)
        {
            return &stack;
        }
    }
    return nullptr;
}

namespace
{
StackModel& EmptyStackFallback()
{
    static StackModel empty;
    return empty;
}

PairingModel& EmptyPairingFallback()
{
    static PairingModel empty;
    return empty;
}
}

PairingModel& GetActivePairing(SessionModel& session)
{
    for (PairingModel& pairing : session.pairings)
    {
        if (pairing.id == session.activePairingId)
        {
            return pairing;
        }
    }
    return session.pairings.empty() ? EmptyPairingFallback() : session.pairings.front();
}

const PairingModel& GetActivePairing(const SessionModel& session)
{
    for (const PairingModel& pairing : session.pairings)
    {
        if (pairing.id == session.activePairingId)
        {
            return pairing;
        }
    }
    return session.pairings.empty() ? EmptyPairingFallback() : session.pairings.front();
}

StackModel& GetActiveFixedStack(SessionModel& session)
{
    PairingModel& pairing = GetActivePairing(session);
    StackModel* stack = FindStack(session, pairing.fixedStackId);
    return stack != nullptr ? *stack : EmptyStackFallback();
}

const StackModel& GetActiveFixedStack(const SessionModel& session)
{
    const PairingModel& pairing = GetActivePairing(session);
    const StackModel* stack = FindStack(session, pairing.fixedStackId);
    return stack != nullptr ? *stack : EmptyStackFallback();
}

StackModel& GetActiveMovingStack(SessionModel& session)
{
    PairingModel& pairing = GetActivePairing(session);
    StackModel* stack = FindStack(session, pairing.movingStackId);
    return stack != nullptr ? *stack : EmptyStackFallback();
}

const StackModel& GetActiveMovingStack(const SessionModel& session)
{
    const PairingModel& pairing = GetActivePairing(session);
    const StackModel* stack = FindStack(session, pairing.movingStackId);
    return stack != nullptr ? *stack : EmptyStackFallback();
}

void RebuildPairs(SessionModel& session, PairingModel& pairing)
{
    StackModel* fixedStack = FindStack(session, pairing.fixedStackId);
    StackModel* movingStack = FindStack(session, pairing.movingStackId);
    if (fixedStack == nullptr || movingStack == nullptr)
    {
        return;
    }

    pairing.globalOffset = pairing.fixedTimelineOffset - pairing.movingTimelineOffset;
    pairing.pairs.clear();

    const int fixedCount = static_cast<int>(fixedStack->slices.size());
    const int movingCount = static_cast<int>(movingStack->slices.size());
    for (int fixedIndex = 0; fixedIndex < fixedCount; ++fixedIndex)
    {
        const int movingIndex = fixedIndex + pairing.globalOffset;

        PairRecord pair;
        pair.fixedIndex = fixedIndex;
        pair.movingIndex = movingIndex;
        pair.valid = movingIndex >= 0 && movingIndex < movingCount;
        pair.status = pair.valid ? PairStatus::Candidate : PairStatus::Unmatched;
        pairing.pairs.push_back(pair);
    }

    const int maxFixed = (std::max)(0, fixedCount - 1);
    const int maxMoving = (std::max)(0, movingCount - 1);
    pairing.activeFixedIndex = (std::clamp)(pairing.activeFixedIndex, 0, maxFixed);
    pairing.activeMovingIndex = (std::clamp)(pairing.activeMovingIndex, 0, maxMoving);

    if (!pairing.pairs.empty() && pairing.activeFixedIndex < fixedCount)
    {
        const PairRecord& pair = pairing.pairs[pairing.activeFixedIndex];
        if (pair.valid)
        {
            pairing.activeMovingIndex = pair.movingIndex;
        }
    }
}
} // namespace align
