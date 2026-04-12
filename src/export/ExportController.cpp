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
                       const cv::Size& targetSize,
                       const Transform2D& transform,
                       const std::filesystem::path& outputPath)
{
    cv::Mat source = cv::imread(sourcePath.string(), cv::IMREAD_COLOR);
    if (source.empty())
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
                                                    const std::filesystem::path& outputPath) const
{
    cv::Mat fixed = cv::imread(fixedPath.string(), cv::IMREAD_COLOR);
    if (fixed.empty())
    {
        return Result{false, "Could not load fixed image to determine export size."};
    }

    return SaveWarpedImage(movingPath, fixed.size(), registration.forward, outputPath);
}

Result ExportController::ExportAlignedFixedToMoving(const std::filesystem::path& fixedPath,
                                                    const std::filesystem::path& movingPath,
                                                    const RegistrationResult& registration,
                                                    const std::filesystem::path& outputPath) const
{
    cv::Mat moving = cv::imread(movingPath.string(), cv::IMREAD_COLOR);
    if (moving.empty())
    {
        return Result{false, "Could not load moving image to determine export size."};
    }

    return SaveWarpedImage(fixedPath, moving.size(), registration.inverse, outputPath);
}
} // namespace align
