#pragma once

#include "core/Types.h"

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

struct PairingModel
{
    StackId fixedStackId;
    StackId movingStackId;
    int globalOffset = 0;
    int fixedTimelineOffset = 0;
    int movingTimelineOffset = 0;
    std::vector<PairRecord> pairs;
};
} // namespace align
