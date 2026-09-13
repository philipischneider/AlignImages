#pragma once

#include "core/Result.h"
#include "data/StackModel.h"

#include <filesystem>
#include <vector>

namespace align
{
// Combines several separate DICOM series (each its own acquisition) that together cover a
// body region too large for a single scan -- e.g. the Visible Human Project's 3-part "Frozen"
// CT (32+34+646) -- into one spatially-ordered StackModel.
class DicomSeriesStitcher
{
public:
    Result LoadStitchedStack(const std::vector<std::filesystem::path>& directories,
                             const std::string& stackId,
                             const std::string& stackName,
                             const std::string& modality,
                             StackModel& stack) const;
};
} // namespace align
