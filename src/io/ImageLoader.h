#pragma once

#include "core/Result.h"

#include <filesystem>

namespace cv
{
class Mat;
}

namespace align
{
class ImageLoader
{
public:
    Result LoadColorImage(const std::filesystem::path& filePath, cv::Mat& image) const;
};
} // namespace align

