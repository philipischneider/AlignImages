#include "registration/RegistrationEngine.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace align
{
namespace
{
struct MaskStats
{
    cv::Point2d center;
    cv::Rect bounds;
    double area = 0.0;
    double angle = 0.0;
};

cv::Mat ExtractCtMask(const cv::Mat& image)
{
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::Mat normalized;
    cv::normalize(gray, normalized, 0, 255, cv::NORM_MINMAX);
    cv::Mat mask;
    cv::threshold(normalized, mask, 20, 255, cv::THRESH_BINARY);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9)));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    return mask;
}

cv::Mat ExtractPhotoMask(const cv::Mat& image)
{
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    cv::Mat blueMask;
    cv::inRange(hsv, cv::Scalar(80, 30, 20), cv::Scalar(145, 255, 255), blueMask);

    cv::Mat bodyMask;
    cv::bitwise_not(blueMask, bodyMask);
    cv::morphologyEx(bodyMask, bodyMask, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(11, 11)));
    cv::morphologyEx(bodyMask, bodyMask, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    return bodyMask;
}

bool ComputeBoundingStats(const cv::Mat& mask, cv::Point2d& center, cv::Rect& bounds, double& area)
{
    std::vector<cv::Point> points;
    cv::findNonZero(mask, points);
    if (points.empty())
    {
        return false;
    }

    bounds = cv::boundingRect(points);
    const cv::Moments moments = cv::moments(mask, true);
    if (moments.m00 <= 0.0)
    {
        return false;
    }

    center = cv::Point2d(moments.m10 / moments.m00, moments.m01 / moments.m00);
    area = moments.m00;
    return true;
}

bool ComputeMaskStats(const cv::Mat& mask, MaskStats& stats)
{
    if (!ComputeBoundingStats(mask, stats.center, stats.bounds, stats.area))
    {
        return false;
    }

    const cv::Moments moments = cv::moments(mask, true);
    const double angle = 0.5 * std::atan2(2.0 * moments.mu11, moments.mu20 - moments.mu02);
    stats.angle = std::isfinite(angle) ? angle : 0.0;
    return true;
}

double NormalizeAngle(double angle)
{
    constexpr double kPi = 3.14159265358979323846;
    while (angle > kPi)
    {
        angle -= 2.0 * kPi;
    }
    while (angle < -kPi)
    {
        angle += 2.0 * kPi;
    }
    return angle;
}

Transform2D BuildSimilarityTransform(double scale, double theta, double tx, double ty)
{
    const double cosTheta = std::cos(theta) * scale;
    const double sinTheta = std::sin(theta) * scale;

    Transform2D transform;
    transform.matrix = {
        cosTheta, -sinTheta, tx,
        sinTheta, cosTheta, ty,
        0.0, 0.0, 1.0
    };
    return transform;
}

Transform2D InvertSimilarityTransform(double scale, double theta, double tx, double ty)
{
    const double safeScale = std::abs(scale) < 1e-6 ? 1.0 : scale;
    const double invScale = 1.0 / safeScale;
    const double invTheta = -theta;
    const double cosTheta = std::cos(invTheta) * invScale;
    const double sinTheta = std::sin(invTheta) * invScale;

    Transform2D transform;
    transform.matrix = {
        cosTheta, -sinTheta, -(cosTheta * tx - sinTheta * ty),
        sinTheta, cosTheta, -(sinTheta * tx + cosTheta * ty),
        0.0, 0.0, 1.0
    };
    return transform;
}

double ComputeMaskOverlapScore(const cv::Mat& movingMask, const cv::Mat& fixedMask, const Transform2D& transform)
{
    cv::Mat affine = (cv::Mat_<double>(2, 3) << transform.matrix[0], transform.matrix[1], transform.matrix[2],
                       transform.matrix[3], transform.matrix[4], transform.matrix[5]);

    cv::Mat warpedMoving;
    cv::warpAffine(movingMask, warpedMoving, affine, fixedMask.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, 0);

    cv::Mat intersectionMask;
    cv::bitwise_and(warpedMoving, fixedMask, intersectionMask);

    const double intersection = static_cast<double>(cv::countNonZero(intersectionMask));
    const double movingArea = static_cast<double>(cv::countNonZero(warpedMoving));
    const double fixedArea = static_cast<double>(cv::countNonZero(fixedMask));
    const double unionArea = movingArea + fixedArea - intersection;

    if (unionArea <= 0.0)
    {
        return 0.0;
    }

    return intersection / unionArea;
}

double ComputeGradientAgreementScore(const cv::Mat& movingImage, const cv::Mat& fixedImage, const Transform2D& transform)
{
    cv::Mat affine = (cv::Mat_<double>(2, 3) << transform.matrix[0], transform.matrix[1], transform.matrix[2],
                       transform.matrix[3], transform.matrix[4], transform.matrix[5]);

    cv::Mat warpedMoving;
    cv::warpAffine(movingImage, warpedMoving, affine, fixedImage.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(0, 0, 0));

    cv::Mat movingGray;
    cv::Mat fixedGray;
    cv::cvtColor(warpedMoving, movingGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(fixedImage, fixedGray, cv::COLOR_BGR2GRAY);

    cv::Mat movingGradX;
    cv::Mat movingGradY;
    cv::Mat fixedGradX;
    cv::Mat fixedGradY;
    cv::Sobel(movingGray, movingGradX, CV_32F, 1, 0, 3);
    cv::Sobel(movingGray, movingGradY, CV_32F, 0, 1, 3);
    cv::Sobel(fixedGray, fixedGradX, CV_32F, 1, 0, 3);
    cv::Sobel(fixedGray, fixedGradY, CV_32F, 0, 1, 3);

    cv::Mat movingMagnitude;
    cv::Mat fixedMagnitude;
    cv::magnitude(movingGradX, movingGradY, movingMagnitude);
    cv::magnitude(fixedGradX, fixedGradY, fixedMagnitude);

    cv::Mat movingNorm;
    cv::Mat fixedNorm;
    cv::normalize(movingMagnitude, movingNorm, 0.0, 1.0, cv::NORM_MINMAX);
    cv::normalize(fixedMagnitude, fixedNorm, 0.0, 1.0, cv::NORM_MINMAX);

    const double sampleCount = movingNorm.total() > 0 ? static_cast<double>(movingNorm.total()) : 1.0;
    const double gradientDistance = cv::norm(movingNorm, fixedNorm, cv::NORM_L1) / sampleCount;
    return (std::max)(0.0, 1.0 - gradientDistance);
}

double ComputeCombinedScore(const cv::Mat& movingMask,
                            const cv::Mat& fixedMask,
                            const cv::Mat& movingImage,
                            const cv::Mat& fixedImage,
                            const Transform2D& transform)
{
    const double maskScore = ComputeMaskOverlapScore(movingMask, fixedMask, transform);
    const double gradientScore = ComputeGradientAgreementScore(movingImage, fixedImage, transform);
    return maskScore * 0.7 + gradientScore * 0.3;
}

void ComputeInitialGuess(const MaskStats& movingStats,
                         const MaskStats& fixedStats,
                         double& scale,
                         double& theta,
                         double& tx,
                         double& ty)
{
    const double movingSize = (std::max)(movingStats.bounds.width, movingStats.bounds.height);
    const double fixedSize = (std::max)(fixedStats.bounds.width, fixedStats.bounds.height);
    scale = movingSize > 0.0 ? fixedSize / movingSize : 1.0;
    theta = NormalizeAngle(fixedStats.angle - movingStats.angle);

    const double cosTheta = std::cos(theta) * scale;
    const double sinTheta = std::sin(theta) * scale;
    tx = fixedStats.center.x - (cosTheta * movingStats.center.x - sinTheta * movingStats.center.y);
    ty = fixedStats.center.y - (sinTheta * movingStats.center.x + cosTheta * movingStats.center.y);
}

void RefineParameters(const cv::Mat& movingMask,
                      const cv::Mat& fixedMask,
                      const cv::Mat& movingImage,
                      const cv::Mat& fixedImage,
                      double& scale,
                      double& theta,
                      double& tx,
                      double& ty,
                      std::vector<IterationRecord>& iterations,
                      double& bestScore)
{
    struct StepConfig
    {
        double txStep;
        double tyStep;
        double thetaStep;
        double scaleStep;
    };

    constexpr double kPi = 3.14159265358979323846;
    const std::array<StepConfig, 3> steps {{
        {16.0, 16.0, 6.0 * kPi / 180.0, 0.08},
        {8.0, 8.0, 3.0 * kPi / 180.0, 0.04},
        {3.0, 3.0, 1.0 * kPi / 180.0, 0.015}
    }};

    for (const StepConfig& step : steps)
    {
        bool improved = true;
        while (improved)
        {
            improved = false;
            double localBestScale = scale;
            double localBestTheta = theta;
            double localBestTx = tx;
            double localBestTy = ty;
            double localBestScore = bestScore;

            for (int thetaOffset = -1; thetaOffset <= 1; ++thetaOffset)
            {
                for (int scaleOffset = -1; scaleOffset <= 1; ++scaleOffset)
                {
                    for (int tyOffset = -1; tyOffset <= 1; ++tyOffset)
                    {
                        for (int txOffset = -1; txOffset <= 1; ++txOffset)
                        {
                            if (thetaOffset == 0 && scaleOffset == 0 && tyOffset == 0 && txOffset == 0)
                            {
                                continue;
                            }

                            const double candidateScale =
                                (std::max)(0.1, scale + step.scaleStep * static_cast<double>(scaleOffset));
                            const double candidateTheta =
                                NormalizeAngle(theta + step.thetaStep * static_cast<double>(thetaOffset));
                            const double candidateTx = tx + step.txStep * static_cast<double>(txOffset);
                            const double candidateTy = ty + step.tyStep * static_cast<double>(tyOffset);

                            const Transform2D candidateTransform =
                                BuildSimilarityTransform(candidateScale, candidateTheta, candidateTx, candidateTy);
                            const double candidateScore = ComputeCombinedScore(
                                movingMask, fixedMask, movingImage, fixedImage, candidateTransform);

                            if (candidateScore > localBestScore)
                            {
                                localBestScale = candidateScale;
                                localBestTheta = candidateTheta;
                                localBestTx = candidateTx;
                                localBestTy = candidateTy;
                                localBestScore = candidateScore;
                                improved = true;
                            }
                        }
                    }
                }
            }

            if (improved)
            {
                scale = localBestScale;
                theta = localBestTheta;
                tx = localBestTx;
                ty = localBestTy;
                bestScore = localBestScore;
                iterations.push_back(
                    {static_cast<int>(iterations.size()), bestScore, tx, ty, theta, scale, false});
            }
        }
    }
}
} // namespace

Result RegistrationEngine::RegisterCtToPhoto(const cv::Mat& movingImage,
                                             const cv::Mat& fixedImage,
                                             RegistrationResult& result) const
{
    if (movingImage.empty() || fixedImage.empty())
    {
        return Result{false, "Images must be loaded before registration."};
    }

    cv::Mat movingMask = ExtractCtMask(movingImage);
    cv::Mat fixedMask = ExtractPhotoMask(fixedImage);

    MaskStats movingStats;
    MaskStats fixedStats;
    if (!ComputeMaskStats(movingMask, movingStats) || !ComputeMaskStats(fixedMask, fixedStats))
    {
        return Result{false, "Could not extract a usable body mask for registration."};
    }

    double scale = 1.0;
    double theta = 0.0;
    double tx = 0.0;
    double ty = 0.0;
    ComputeInitialGuess(movingStats, fixedStats, scale, theta, tx, ty);

    result.transformType = "similarity";
    result.iterations.clear();

    result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
    double bestScore = ComputeCombinedScore(movingMask, fixedMask, movingImage, fixedImage, result.forward);
    result.iterations.push_back({0, bestScore, tx, ty, theta, scale, false});

    RefineParameters(movingMask, fixedMask, movingImage, fixedImage, scale, theta, tx, ty, result.iterations, bestScore);

    result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
    result.inverse = InvertSimilarityTransform(scale, theta, tx, ty);
    result.score = bestScore;
    result.converged = true;

    if (!result.iterations.empty())
    {
        result.iterations.back().converged = true;
    }

    return Result{};
}

Result RegistrationEngine::RefineCtToPhotoFromPrior(const cv::Mat& movingImage,
                                                    const cv::Mat& fixedImage,
                                                    double priorTx,
                                                    double priorTy,
                                                    double priorTheta,
                                                    double priorScale,
                                                    RegistrationResult& result) const
{
    if (movingImage.empty() || fixedImage.empty())
    {
        return Result{false, "Images must be loaded before refinement."};
    }

    cv::Mat movingMask = ExtractCtMask(movingImage);
    cv::Mat fixedMask = ExtractPhotoMask(fixedImage);

    result.transformType = "similarity_prior_refined";
    result.iterations.clear();

    double scale = priorScale;
    double theta = priorTheta;
    double tx = priorTx;
    double ty = priorTy;

    result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
    double bestScore = ComputeCombinedScore(movingMask, fixedMask, movingImage, fixedImage, result.forward);
    result.iterations.push_back({0, bestScore, tx, ty, theta, scale, false});

    RefineParameters(movingMask, fixedMask, movingImage, fixedImage, scale, theta, tx, ty, result.iterations, bestScore);

    result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
    result.inverse = InvertSimilarityTransform(scale, theta, tx, ty);
    result.score = bestScore;
    result.converged = true;
    result.refinedWithPrior = true;
    result.hasConvergencePrior = true;
    result.priorTx = priorTx;
    result.priorTy = priorTy;
    result.priorTheta = priorTheta;
    result.priorScale = priorScale;

    if (!result.iterations.empty())
    {
        result.iterations.back().converged = true;
    }

    return Result{};
}
} // namespace align
