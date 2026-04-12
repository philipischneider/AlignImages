#include "io/ImageLoader.h"

#include <opencv2/imgcodecs.hpp>

namespace align
{
Result ImageLoader::LoadColorImage(const std::filesystem::path& filePath, cv::Mat& image) const
{
    image = cv::imread(filePath.string(), cv::IMREAD_COLOR);
    if (image.empty())
    {
        return Result{false, "Could not load image from disk."};
    }

    return Result{};
}
} // namespace align

