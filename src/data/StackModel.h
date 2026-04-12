#pragma once

#include "data/SliceRecord.h"

#include <string>
#include <vector>

namespace align
{
struct StackModel
{
    StackId id;
    std::string name;
    std::string modality;
    std::string directory;
    std::vector<SliceRecord> slices;
};
} // namespace align

