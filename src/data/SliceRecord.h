#pragma once

#include "core/Types.h"

#include <string>

namespace align
{
enum class SliceStatus
{
    None,
    Candidate,
    Aligned,
    Suspect,
    Manual,
    Unmatched
};

struct SliceRecord
{
    SliceIndex stackIndex = -1;
    std::string filePath;
    std::string fileName;
    int width = 0;
    int height = 0;
    bool hasThumbnail = false;
    SliceStatus status = SliceStatus::None;
    int instanceNumber = -1;
    double sliceLocation = 0.0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    int rotationDegrees = 0; // 0, 90, 180, 270 -- clockwise
};
} // namespace align

