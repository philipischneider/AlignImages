#include "registration/LandmarkRegistration.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

#include <cmath>
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
} // namespace

Result LandmarkRegistration::ComputeFromLandmarks(RegistrationResult& registration) const
{
    if (registration.landmarks.size() < 2)
    {
        return Result{false, "At least two landmark pairs are required."};
    }

    std::vector<cv::Point2f> movingPoints;
    std::vector<cv::Point2f> fixedPoints;
    movingPoints.reserve(registration.landmarks.size());
    fixedPoints.reserve(registration.landmarks.size());

    for (const LandmarkPair& landmark : registration.landmarks)
    {
        movingPoints.emplace_back(static_cast<float>(landmark.movingX), static_cast<float>(landmark.movingY));
        fixedPoints.emplace_back(static_cast<float>(landmark.fixedX), static_cast<float>(landmark.fixedY));
    }

    cv::Mat inliers;
    cv::Mat affine = cv::estimateAffinePartial2D(movingPoints, fixedPoints, inliers, cv::RANSAC, 3.0);
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
