#pragma once

#include "core/Result.h"
#include "data/RegistrationResult.h"
#include "io/ImageLoader.h"

#include <filesystem>

namespace align
{
class ExportController
{
public:
    Result ExportAlignedMovingToFixed(const std::filesystem::path& movingPath,
                                      const std::filesystem::path& fixedPath,
                                      const RegistrationResult& registration,
                                      const std::filesystem::path& outputPath,
                                      const ImageLoadOptions& movingWindow,
                                      const ImageLoadOptions& fixedWindow) const;

    Result ExportAlignedFixedToMoving(const std::filesystem::path& fixedPath,
                                      const std::filesystem::path& movingPath,
                                      const RegistrationResult& registration,
                                      const std::filesystem::path& outputPath,
                                      const ImageLoadOptions& fixedWindow,
                                      const ImageLoadOptions& movingWindow) const;
};
} // namespace align
