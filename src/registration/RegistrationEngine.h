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

    // Normalized mutual information (NMI, in [0,1]) between the fixed image and the moving image
    // warped by `transform`. Self-contained (no pyramid/LevelData needed) and downsamples
    // internally for speed, so it's cheap enough to call every frame for a live "how well
    // aligned is this right now" readout while the user drags landmarks or the Transpose gizmo --
    // unlike the mask-overlap/gradient score used by the optimizer, MI doesn't assume any direct
    // intensity relationship between the two images, which suits cross-modality pairs like CT vs
    // photo (see MUTUAL_INFORMATION_PLAN.md).
    double ComputeMutualInformationScore(const cv::Mat& movingImage,
                                         const cv::Mat& fixedImage,
                                         const Transform2D& transform) const;
};
} // namespace align
