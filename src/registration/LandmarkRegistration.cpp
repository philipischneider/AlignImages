#include "registration/LandmarkRegistration.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

#include <cmath>
#include <numeric>
#include <vector>

namespace align
{
namespace
{
Transform2D FromAffine2x3(const cv::Mat& affine)
{
    Transform2D transform;
    transform.matrix = {
        affine.at<double>(0, 0), affine.at<double>(0, 1), affine.at<double>(0, 2),
        affine.at<double>(1, 0), affine.at<double>(1, 1), affine.at<double>(1, 2),
        0.0, 0.0, 1.0
    };
    return transform;
}

Transform2D InvertTransform(const Transform2D& transform)
{
    cv::Mat full = (cv::Mat_<double>(3, 3) << transform.matrix[0], transform.matrix[1], transform.matrix[2],
                    transform.matrix[3], transform.matrix[4], transform.matrix[5],
                    transform.matrix[6], transform.matrix[7], transform.matrix[8]);
    cv::Mat inverse = full.inv();
    Transform2D result;
    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            result.matrix[row * 3 + col] = inverse.at<double>(row, col);
        }
    }
    return result;
}

double ComputeRmsError(const std::vector<cv::Point2f>& moving,
                       const std::vector<cv::Point2f>& fixed,
                       const cv::Mat& affine)
{
    std::vector<cv::Point2f> transformed;
    cv::transform(moving, transformed, affine);

    double total = 0.0;
    for (size_t i = 0; i < fixed.size(); ++i)
    {
        const double dx = static_cast<double>(transformed[i].x - fixed[i].x);
        const double dy = static_cast<double>(transformed[i].y - fixed[i].y);
        total += dx * dx + dy * dy;
    }

    return std::sqrt(total / static_cast<double>(fixed.size()));
}

cv::Mat EstimateSimilarityLeastSquares(const std::vector<cv::Point2f>& moving,
                                       const std::vector<cv::Point2f>& fixed)
{
    if (moving.size() != fixed.size() || moving.size() < 2)
    {
        return {};
    }

    cv::Point2d movingCenter(0.0, 0.0);
    cv::Point2d fixedCenter(0.0, 0.0);
    for (size_t i = 0; i < moving.size(); ++i)
    {
        movingCenter.x += moving[i].x;
        movingCenter.y += moving[i].y;
        fixedCenter.x += fixed[i].x;
        fixedCenter.y += fixed[i].y;
    }

    const double count = static_cast<double>(moving.size());
    movingCenter.x /= count;
    movingCenter.y /= count;
    fixedCenter.x /= count;
    fixedCenter.y /= count;

    double dot = 0.0;
    double cross = 0.0;
    double movingNormSq = 0.0;

    for (size_t i = 0; i < moving.size(); ++i)
    {
        const double mx = static_cast<double>(moving[i].x) - movingCenter.x;
        const double my = static_cast<double>(moving[i].y) - movingCenter.y;
        const double fx = static_cast<double>(fixed[i].x) - fixedCenter.x;
        const double fy = static_cast<double>(fixed[i].y) - fixedCenter.y;

        dot += mx * fx + my * fy;
        cross += mx * fy - my * fx;
        movingNormSq += mx * mx + my * my;
    }

    if (movingNormSq <= 1e-9)
    {
        return {};
    }

    const double scale = std::sqrt(dot * dot + cross * cross) / movingNormSq;
    const double theta = std::atan2(cross, dot);
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double tx = fixedCenter.x - scale * (c * movingCenter.x - s * movingCenter.y);
    const double ty = fixedCenter.y - scale * (s * movingCenter.x + c * movingCenter.y);

    return (cv::Mat_<double>(2, 3) <<
        scale * c, -scale * s, tx,
        scale * s,  scale * c, ty);
}
} // namespace

Result LandmarkRegistration::ComputeFromLandmarks(RegistrationResult& registration) const
{
    std::vector<cv::Point2f> movingPoints;
    std::vector<cv::Point2f> fixedPoints;
    movingPoints.reserve(registration.landmarks.size());
    fixedPoints.reserve(registration.landmarks.size());

    for (const LandmarkPair& landmark : registration.landmarks)
    {
        if (!std::isfinite(landmark.fixedX) || !std::isfinite(landmark.fixedY))
            continue;
        movingPoints.emplace_back(static_cast<float>(landmark.movingX), static_cast<float>(landmark.movingY));
        fixedPoints.emplace_back(static_cast<float>(landmark.fixedX), static_cast<float>(landmark.fixedY));
    }

    if (movingPoints.size() < 2)
    {
        return Result{false, "At least two complete landmark pairs are required."};
    }

    cv::Mat affine = EstimateSimilarityLeastSquares(movingPoints, fixedPoints);
    if (affine.empty())
    {
        return Result{false, "Could not estimate a similarity transform from the landmarks."};
    }

    registration.transformType = "manual_landmarks";
    registration.forward = FromAffine2x3(affine);
    registration.inverse = InvertTransform(registration.forward);
    registration.manualRmsError = ComputeRmsError(movingPoints, fixedPoints, affine);
    registration.score = 1.0 / (1.0 + registration.manualRmsError);
    registration.converged = true;
    registration.isManual = true;
    registration.isInterpolated = false;
    registration.iterations.clear();

    const double tx = affine.at<double>(0, 2);
    const double ty = affine.at<double>(1, 2);
    const double theta = std::atan2(affine.at<double>(1, 0), affine.at<double>(0, 0));
    const double scale = std::sqrt(affine.at<double>(0, 0) * affine.at<double>(0, 0) +
                                   affine.at<double>(1, 0) * affine.at<double>(1, 0));
    registration.iterations.push_back({0, registration.score, tx, ty, theta, scale, true});
    return Result{};
}
} // namespace align
