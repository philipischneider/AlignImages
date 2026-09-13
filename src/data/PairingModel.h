#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace align
{
enum class PairStatus
{
    Unmatched,
    Candidate,
    Aligned,
    Suspect,
    Manual
};

struct PairRecord
{
    SliceIndex fixedIndex = -1;
    SliceIndex movingIndex = -1;
    bool valid = false;
    PairStatus status = PairStatus::Unmatched;
};

const char* ToString(PairStatus status);

std::string MakePairingId(const StackId& fixedStackId, const StackId& movingStackId);

struct PairingModel
{
    std::string id;
    std::string label;
    StackId fixedStackId;
    StackId movingStackId;
    int globalOffset = 0;
    int fixedTimelineOffset = 0;
    int movingTimelineOffset = 0;
    int activeFixedIndex = 0;
    int activeMovingIndex = 0;
    std::vector<PairRecord> pairs;
};
} // namespace align
