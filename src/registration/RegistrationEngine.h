#pragma once

#include "core/Result.h"
#include "data/RegistrationResult.h"

namespace cv
{
class Mat;
}

namespace align
{
class RegistrationEngine
{
public:
    Result RegisterCtToPhoto(const cv::Mat& movingImage, const cv::Mat& fixedImage, RegistrationResult& result) const;
    Result RefineCtToPhotoFromPrior(const cv::Mat& movingImage,
                                    const cv::Mat& fixedImage,
                                    double priorTx,
                                    double priorTy,
                                    double priorTheta,
                                    double priorScale,
                                    RegistrationResult& result) const;
};
} // namespace align
