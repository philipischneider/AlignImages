#include "io/ImageLoader.h"

#include "data/SliceRecord.h"
#include "data/StackModel.h"
#include "io/DicomLoader.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>

namespace align
{
namespace
{
bool HasDicomExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".dcm";
}
} // namespace

void ApplyOrientation(cv::Mat& image, const ImageLoadOptions& options)
{
    // Rotation is applied before flipping so the combination is well-defined regardless of order chosen in the UI.
    switch (options.rotationDegrees)
    {
    case 90:
        cv::rotate(image, image, cv::ROTATE_90_CLOCKWISE);
        break;
    case 180:
        cv::rotate(image, image, cv::ROTATE_180);
        break;
    case 270:
        cv::rotate(image, image, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    default:
        break;
    }

    if (options.flipHorizontal && options.flipVertical)
    {
        cv::flip(image, image, -1);
    }
    else if (options.flipHorizontal)
    {
        cv::flip(image, image, 1);
    }
    else if (options.flipVertical)
    {
        cv::flip(image, image, 0);
    }
}

ImageLoadOptions MakeDefaultLoadOptions(const StackModel& stack, const SliceRecord& slice)
{
    return ImageLoadOptions{stack.isDicom,          stack.rescaleSlope,     stack.rescaleIntercept,
                            stack.defaultWindowCenter, stack.defaultWindowWidth,
                            slice.flipHorizontal,   slice.flipVertical,     slice.rotationDegrees};
}

ImageLoadOptions MakeDisplayLoadOptions(const StackModel& stack, const SliceRecord& slice)
{
    return ImageLoadOptions{stack.isDicom,      stack.rescaleSlope, stack.rescaleIntercept,
                            stack.windowCenter, stack.windowWidth,
                            slice.flipHorizontal, slice.flipVertical, slice.rotationDegrees};
}

ImageLoadOptions WithOrientation(ImageLoadOptions options, const SliceRecord& slice)
{
    options.flipHorizontal = slice.flipHorizontal;
    options.flipVertical = slice.flipVertical;
    options.rotationDegrees = slice.rotationDegrees;
    return options;
}

Result ImageLoader::LoadColorImage(const std::filesystem::path& filePath, cv::Mat& image, const ImageLoadOptions& options) const
{
    if (options.isDicom || HasDicomExtension(filePath))
    {
        DicomLoader dicomLoader;
        cv::Mat raw;
        const Result decodeResult = dicomLoader.ReadPixelData(filePath, raw);
        if (!decodeResult.ok)
        {
            return decodeResult;
        }

        image = dicomLoader.ApplyWindowLevel(raw, options.rescaleSlope, options.rescaleIntercept,
                                             options.windowCenter, options.windowWidth);
        if (image.empty())
        {
            return Result{false, "Could not remap DICOM pixel data for display."};
        }
        ApplyOrientation(image, options);
        return Result{};
    }

    image = cv::imread(filePath.string(), cv::IMREAD_COLOR);
    if (image.empty())
    {
        return Result{false, "Could not load image from disk."};
    }

    ApplyOrientation(image, options);
    return Result{};
}
} // namespace align
