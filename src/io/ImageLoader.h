#pragma once

#include "core/Result.h"

#include <filesystem>

namespace cv
{
class Mat;
}

namespace align
{
struct StackModel;
struct SliceRecord;

// Lightweight, copyable subset of a StackModel/SliceRecord's display context. Kept separate
// from StackModel so async task lambdas don't need to copy an entire slice list just to
// load one image.
struct ImageLoadOptions
{
    bool isDicom = false;
    double rescaleSlope = 1.0;
    double rescaleIntercept = 0.0;
    double windowCenter = 0.0;
    double windowWidth = 0.0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    int rotationDegrees = 0; // 0, 90, 180, 270 -- clockwise
};

ImageLoadOptions MakeDefaultLoadOptions(const StackModel& stack, const SliceRecord& slice);
ImageLoadOptions MakeDisplayLoadOptions(const StackModel& stack, const SliceRecord& slice);

// Copies the per-slice orientation fields from `slice` onto `options` and returns it. Useful
// when the DICOM portion of the options was already computed once per stack outside of a loop,
// and only the orientation needs to vary per slice within that loop.
ImageLoadOptions WithOrientation(ImageLoadOptions options, const SliceRecord& slice);

// Applies rotation (multiples of 90 degrees, clockwise) then horizontal/vertical flip in place.
void ApplyOrientation(cv::Mat& image, const ImageLoadOptions& options);

class ImageLoader
{
public:
    // options.windowCenter/windowWidth are used verbatim for the remap; callers decide
    // whether to pass the fixed "default" window (registration/export/batch, for
    // reproducibility) or the live interactive display window (viewers/timeline).
    // For non-DICOM stacks, pass a default-constructed ImageLoadOptions{}.
    Result LoadColorImage(const std::filesystem::path& filePath, cv::Mat& image, const ImageLoadOptions& options) const;
};
} // namespace align
