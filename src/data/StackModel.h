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

    bool isDicom = false;
    double windowCenter = 0.0;
    double windowWidth = 0.0;
    double defaultWindowCenter = 0.0;
    double defaultWindowWidth = 0.0;
    double rescaleSlope = 1.0;
    double rescaleIntercept = 0.0;
};
} // namespace align

