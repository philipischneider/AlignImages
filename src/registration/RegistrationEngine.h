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
    Result RegisterCtToPhoto(const cv::Mat& movingImage,
                             const cv::Mat& fixedImage,
                             RegistrationResult& result,
                             bool useAffine = false) const;

    Result RefineCtToPhotoFromPrior(const cv::Mat& movingImage,
                                    const cv::Mat& fixedImage,
                                    double priorTx,
                                    double priorTy,
                                    double priorTheta,
                                    double priorScale,
                                    RegistrationResult& result,
                                    bool useAffine = false,
                                    double priorSx = -1.0,
                                    double priorSy = -1.0,
                                    bool useSigmaBounds = false,
                                    double sigmaMultiplier = 1.5,
                                    double priorTxStdDev = -1.0,
                                    double priorTyStdDev = -1.0,
                                    double priorThetaStdDev = -1.0,
                                    double priorScaleStdDev = -1.0,
                                    double priorSxStdDev = -1.0,
                                    double priorSyStdDev = -1.0) const;
};
} // namespace align
