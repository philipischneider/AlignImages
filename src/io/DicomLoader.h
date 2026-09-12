#pragma once

#include "core/Result.h"

#include <filesystem>

namespace cv
{
class Mat;
}

namespace align
{
struct DicomMetadata
{
    int width = 0;
    int height = 0;
    int instanceNumber = -1;
    double sliceLocation = 0.0;
    std::string modality;
    double rescaleSlope = 1.0;
    double rescaleIntercept = 0.0;
    double windowCenter = 0.0;
    double windowWidth = 0.0;
    bool hasWindowTag = false;
};

class DicomLoader
{
public:
    // Reads header/tag metadata only, without decoding pixel data. Cheap; used during
    // directory scanning to order slices and seed default window/level.
    Result ReadHeaderMetadata(const std::filesystem::path& filePath, DicomMetadata& metadata) const;

    // Decodes pixel data into its native bit depth (e.g. CV_16UC1). Does not apply any
    // rescale/window remap.
    Result ReadPixelData(const std::filesystem::path& filePath, cv::Mat& rawOut) const;

    // Pure remap from native pixel data to an 8-bit BGR Mat suitable for the rest of the
    // pipeline: HU = raw * slope + intercept, then linear window to [0,255].
    cv::Mat ApplyWindowLevel(const cv::Mat& raw, double slope, double intercept, double center, double width) const;
};
} // namespace align
