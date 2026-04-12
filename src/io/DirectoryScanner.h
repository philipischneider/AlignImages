#pragma once

#include "core/Result.h"
#include "data/StackModel.h"

#include <filesystem>

namespace align
{
class DirectoryScanner
{
public:
    Result LoadStackFromDirectory(const std::filesystem::path& directory,
                                  const std::string& stackId,
                                  const std::string& stackName,
                                  const std::string& modality,
                                  StackModel& stack) const;
};
} // namespace align

