#include "io/DicomLoader.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <gdcmAttribute.h>
#include <gdcmImage.h>
#include <gdcmImageReader.h>
#include <gdcmReader.h>

#include <algorithm>
#include <cstring>

namespace align
{
namespace
{
template <uint16_t Group, uint16_t Element, typename T>
bool TryReadAttribute(const gdcm::DataSet& dataSet, T& outValue)
{
    if (!dataSet.FindDataElement(gdcm::Tag(Group, Element)))
    {
        return false;
    }

    gdcm::Attribute<Group, Element> attribute;
    attribute.SetFromDataSet(dataSet);
    if (attribute.GetNumberOfValues() == 0)
    {
        return false;
    }

    outValue = attribute.GetValue();
    return true;
}
} // namespace

Result DicomLoader::ReadHeaderMetadata(const std::filesystem::path& filePath, DicomMetadata& metadata) const
{
    gdcm::Reader reader;
    reader.SetFileName(filePath.string().c_str());
    if (!reader.Read())
    {
        return Result{false, "Could not read DICOM header."};
    }

    const gdcm::DataSet& dataSet = reader.GetFile().GetDataSet();

    unsigned short rows = 0;
    unsigned short columns = 0;
    TryReadAttribute<0x0028, 0x0010>(dataSet, rows);
    TryReadAttribute<0x0028, 0x0011>(dataSet, columns);
    metadata.height = static_cast<int>(rows);
    metadata.width = static_cast<int>(columns);

    int instanceNumber = -1;
    if (TryReadAttribute<0x0020, 0x0013>(dataSet, instanceNumber))
    {
        metadata.instanceNumber = instanceNumber;
    }

    double sliceLocation = 0.0;
    if (TryReadAttribute<0x0020, 0x1041>(dataSet, sliceLocation))
    {
        metadata.sliceLocation = sliceLocation;
    }

    std::string modality;
    if (TryReadAttribute<0x0008, 0x0060>(dataSet, modality))
    {
        metadata.modality = modality;
    }

    double slope = 1.0;
    if (TryReadAttribute<0x0028, 0x1053>(dataSet, slope))
    {
        metadata.rescaleSlope = slope;
    }

    double intercept = 0.0;
    if (TryReadAttribute<0x0028, 0x1052>(dataSet, intercept))
    {
        metadata.rescaleIntercept = intercept;
    }

    double windowCenter = 0.0;
    double windowWidth = 0.0;
    const bool hasCenter = TryReadAttribute<0x0028, 0x1050>(dataSet, windowCenter);
    const bool hasWidth = TryReadAttribute<0x0028, 0x1051>(dataSet, windowWidth);
    if (hasCenter && hasWidth && windowWidth > 0.0)
    {
        metadata.windowCenter = windowCenter;
        metadata.windowWidth = windowWidth;
        metadata.hasWindowTag = true;
    }

    return Result{};
}

Result DicomLoader::ReadPixelData(const std::filesystem::path& filePath, cv::Mat& rawOut) const
{
    gdcm::ImageReader reader;
    reader.SetFileName(filePath.string().c_str());
    if (!reader.Read())
    {
        return Result{false, "Could not decode DICOM pixel data."};
    }

    const gdcm::Image& image = reader.GetImage();
    const unsigned int* dimensions = image.GetDimensions();
    const int width = static_cast<int>(dimensions[0]);
    const int height = static_cast<int>(dimensions[1]);
    if (width <= 0 || height <= 0)
    {
        return Result{false, "DICOM image has invalid dimensions."};
    }

    const gdcm::PixelFormat& pixelFormat = image.GetPixelFormat();
    const unsigned short samplesPerPixel = pixelFormat.GetSamplesPerPixel();
    const unsigned short bitsAllocated = pixelFormat.GetBitsAllocated();
    const bool isSigned = pixelFormat.GetPixelRepresentation() != 0;

    std::vector<char> buffer(image.GetBufferLength());
    if (!image.GetBuffer(buffer.data()))
    {
        return Result{false, "Could not read DICOM pixel buffer."};
    }

    if (samplesPerPixel >= 3)
    {
        cv::Mat rgb(height, width, CV_8UC3, buffer.data());
        cv::cvtColor(rgb.clone(), rawOut, cv::COLOR_RGB2BGR);
        return Result{};
    }

    if (bitsAllocated == 8)
    {
        rawOut = cv::Mat(height, width, CV_8UC1, buffer.data()).clone();
        return Result{};
    }

    if (bitsAllocated == 16)
    {
        rawOut = cv::Mat(height, width, isSigned ? CV_16SC1 : CV_16UC1, buffer.data()).clone();
        return Result{};
    }

    return Result{false, "Unsupported DICOM pixel bit depth."};
}

cv::Mat DicomLoader::ApplyWindowLevel(const cv::Mat& raw, double slope, double intercept, double center, double width) const
{
    if (raw.channels() >= 3)
    {
        cv::Mat bgr;
        if (raw.type() != CV_8UC3)
        {
            raw.convertTo(bgr, CV_8UC3);
        }
        else
        {
            bgr = raw;
        }
        return bgr;
    }

    cv::Mat hu;
    raw.convertTo(hu, CV_64F, slope, intercept);

    const double safeWidth = (std::max)(width, 1.0);
    const double low = center - safeWidth / 2.0;

    cv::Mat gray8;
    hu.convertTo(gray8, CV_8UC1, 255.0 / safeWidth, -low * 255.0 / safeWidth);

    cv::Mat bgr;
    cv::cvtColor(gray8, bgr, cv::COLOR_GRAY2BGR);
    return bgr;
}
} // namespace align
