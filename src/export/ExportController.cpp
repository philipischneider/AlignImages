#include "export/ExportController.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace align
{
namespace
{
cv::Mat BuildAffine(const Transform2D& transform)
{
    return (cv::Mat_<double>(2, 3) << transform.matrix[0], transform.matrix[1], transform.matrix[2],
            transform.matrix[3], transform.matrix[4], transform.matrix[5]);
}

Result SaveWarpedImage(const std::filesystem::path& sourcePath,
                       const ImageLoadOptions& sourceWindow,
                       const cv::Size& targetSize,
                       const Transform2D& transform,
                       const std::filesystem::path& outputPath)
{
    cv::Mat source;
    const Result loadResult = ImageLoader{}.LoadColorImage(sourcePath, source, sourceWindow);
    if (!loadResult.ok || source.empty())
    {
        return Result{false, "Could not load source image for export."};
    }

    cv::Mat warped;
    cv::warpAffine(source, warped, BuildAffine(transform), targetSize, cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(0, 0, 0));

    std::filesystem::create_directories(outputPath.parent_path());
    if (!cv::imwrite(outputPath.string(), warped))
    {
        return Result{false, "Could not save exported image."};
    }

    return Result{};
}
} // namespace

Result ExportController::ExportAlignedMovingToFixed(const std::filesystem::path& movingPath,
                                                    const std::filesystem::path& fixedPath,
                                                    const RegistrationResult& registration,
                                                    const std::filesystem::path& outputPath,
                                                    const ImageLoadOptions& movingWindow,
                                                    const ImageLoadOptions& fixedWindow) const
{
    cv::Mat fixed;
    const Result loadResult = ImageLoader{}.LoadColorImage(fixedPath, fixed, fixedWindow);
    if (!loadResult.ok || fixed.empty())
    {
        return Result{false, "Could not load fixed image to determine export size."};
    }

    return SaveWarpedImage(movingPath, movingWindow, fixed.size(), registration.forward, outputPath);
}

Result ExportController::ExportAlignedFixedToMoving(const std::filesystem::path& fixedPath,
                                                    const std::filesystem::path& movingPath,
                                                    const RegistrationResult& registration,
                                                    const std::filesystem::path& outputPath,
                                                    const ImageLoadOptions& fixedWindow,
                                                    const ImageLoadOptions& movingWindow) const
{
    cv::Mat moving;
    const Result loadResult = ImageLoader{}.LoadColorImage(movingPath, moving, movingWindow);
    if (!loadResult.ok || moving.empty())
    {
        return Result{false, "Could not load moving image to determine export size."};
    }

    return SaveWarpedImage(fixedPath, fixedWindow, moving.size(), registration.inverse, outputPath);
}
} // namespace align
