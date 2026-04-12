#include "registration/RegistrationEngine.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <vector>

namespace align
{
namespace
{

// ============================================================
// Mask statistics
// ============================================================

struct MaskStats
{
    cv::Point2d center;
    cv::Rect bounds;
    double area = 0.0;
    double angle = 0.0;
};

// ============================================================
// Mask extraction — structuring elements are built once
// ============================================================

cv::Mat ExtractCtMask(const cv::Mat& image)
{
    static const cv::Mat kCloseKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9));
    static const cv::Mat kOpenKernel  = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::Mat normalized;
    cv::normalize(gray, normalized, 0, 255, cv::NORM_MINMAX);
    cv::Mat mask;
    cv::threshold(normalized, mask, 20, 255, cv::THRESH_BINARY);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kCloseKernel);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,  kOpenKernel);
    return mask;
}

cv::Mat ExtractPhotoMask(const cv::Mat& image)
{
    static const cv::Mat kCloseKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(11, 11));
    static const cv::Mat kOpenKernel  = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));

    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::Mat blueMask;
    cv::inRange(hsv, cv::Scalar(80, 30, 20), cv::Scalar(145, 255, 255), blueMask);
    cv::Mat bodyMask;
    cv::bitwise_not(blueMask, bodyMask);
    cv::morphologyEx(bodyMask, bodyMask, cv::MORPH_CLOSE, kCloseKernel);
    cv::morphologyEx(bodyMask, bodyMask, cv::MORPH_OPEN,  kOpenKernel);
    return bodyMask;
}

// ============================================================
// Mask statistics — cv::moments computed only once
// ============================================================

bool ComputeMaskStats(const cv::Mat& mask, MaskStats& stats)
{
    std::vector<cv::Point> points;
    cv::findNonZero(mask, points);
    if (points.empty())
        return false;

    const cv::Moments moments = cv::moments(mask, true);
    if (moments.m00 <= 0.0)
        return false;

    stats.area   = moments.m00;
    stats.center = cv::Point2d(moments.m10 / moments.m00, moments.m01 / moments.m00);
    stats.bounds = cv::boundingRect(points);

    const double angle = 0.5 * std::atan2(2.0 * moments.mu11, moments.mu20 - moments.mu02);
    stats.angle = std::isfinite(angle) ? angle : 0.0;
    return true;
}

// ============================================================
// Math utilities
// ============================================================

double NormalizeAngle(double angle)
{
    constexpr double kPi = 3.14159265358979323846;
    while (angle >  kPi) angle -= 2.0 * kPi;
    while (angle < -kPi) angle += 2.0 * kPi;
    return angle;
}

Transform2D BuildSimilarityTransform(double scale, double theta, double tx, double ty)
{
    const double cosTheta = std::cos(theta) * scale;
    const double sinTheta = std::sin(theta) * scale;
    Transform2D transform;
    transform.matrix = {
        cosTheta, -sinTheta, tx,
        sinTheta,  cosTheta, ty,
        0.0, 0.0, 1.0
    };
    return transform;
}

Transform2D InvertSimilarityTransform(double scale, double theta, double tx, double ty)
{
    const double safeScale = std::abs(scale) < 1e-6 ? 1.0 : scale;
    const double invScale  = 1.0 / safeScale;
    const double invTheta  = -theta;
    const double cosTheta  = std::cos(invTheta) * invScale;
    const double sinTheta  = std::sin(invTheta) * invScale;
    Transform2D transform;
    transform.matrix = {
        cosTheta, -sinTheta, -(cosTheta * tx - sinTheta * ty),
        sinTheta,  cosTheta, -(sinTheta * tx + cosTheta * ty),
        0.0, 0.0, 1.0
    };
    return transform;
}

void ComputeInitialGuess(const MaskStats& movingStats,
                         const MaskStats& fixedStats,
                         double& scale,
                         double& theta,
                         double& tx,
                         double& ty)
{
    const double movingSize = (std::max)(movingStats.bounds.width, movingStats.bounds.height);
    const double fixedSize  = (std::max)(fixedStats.bounds.width,  fixedStats.bounds.height);
    scale = movingSize > 0.0 ? fixedSize / movingSize : 1.0;
    theta = NormalizeAngle(fixedStats.angle - movingStats.angle);

    const double cosTheta = std::cos(theta) * scale;
    const double sinTheta = std::sin(theta) * scale;
    tx = fixedStats.center.x - (cosTheta * movingStats.center.x - sinTheta * movingStats.center.y);
    ty = fixedStats.center.y - (sinTheta * movingStats.center.x + cosTheta * movingStats.center.y);
}

// ============================================================
// Pyramid level — pre-computed fixed image data
// ============================================================

struct LevelData
{
    cv::Mat movingMask;
    cv::Mat fixedMask;
    cv::Mat movingImage;
    // Pre-computed fixed image gradient norm (avoids recomputing for every candidate)
    cv::Mat fixedNorm;
    // Pre-computed fixed mask area (avoids countNonZero on every candidate)
    double  fixedArea  = 0.0;
    // 1.0 / downsample: converts tx/ty from original-image coordinates to this level
    double  coordScale = 1.0;
};

cv::Mat ComputeGradientNorm(const cv::Mat& colorImage)
{
    cv::Mat gray;
    cv::cvtColor(colorImage, gray, cv::COLOR_BGR2GRAY);
    cv::Mat gradX, gradY;
    cv::Sobel(gray, gradX, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gradY, CV_32F, 0, 1, 3);
    cv::Mat magnitude;
    cv::magnitude(gradX, gradY, magnitude);
    cv::Mat norm;
    cv::normalize(magnitude, norm, 0.0, 1.0, cv::NORM_MINMAX);
    return norm;
}

LevelData BuildLevelData(const cv::Mat& movingMask,
                         const cv::Mat& fixedMask,
                         const cv::Mat& movingImage,
                         const cv::Mat& fixedImage,
                         int downsample)
{
    LevelData level;
    level.coordScale = 1.0 / static_cast<double>(downsample);

    if (downsample == 1)
    {
        level.movingMask  = movingMask;
        level.fixedMask   = fixedMask;
        level.movingImage = movingImage;
        // Pre-compute fixed gradient norm at full resolution
        level.fixedNorm   = ComputeGradientNorm(fixedImage);
    }
    else
    {
        const cv::Size sz(fixedImage.cols / downsample, fixedImage.rows / downsample);
        cv::resize(movingMask,  level.movingMask,  sz, 0, 0, cv::INTER_NEAREST);
        cv::resize(fixedMask,   level.fixedMask,   sz, 0, 0, cv::INTER_NEAREST);
        cv::resize(movingImage, level.movingImage, sz, 0, 0, cv::INTER_AREA);
        cv::Mat fixedImageDown;
        cv::resize(fixedImage, fixedImageDown, sz, 0, 0, cv::INTER_AREA);
        level.fixedNorm = ComputeGradientNorm(fixedImageDown);
    }

    level.fixedArea = static_cast<double>(cv::countNonZero(level.fixedMask));
    return level;
}

// ============================================================
// Scoring functions — accept pre-computed fixed data
// ============================================================

double ComputeMaskOverlapScore(const cv::Mat& movingMask,
                               const cv::Mat& fixedMask,
                               double precomputedFixedArea,
                               const Transform2D& transform)
{
    cv::Mat affine = (cv::Mat_<double>(2, 3) <<
        transform.matrix[0], transform.matrix[1], transform.matrix[2],
        transform.matrix[3], transform.matrix[4], transform.matrix[5]);

    cv::Mat warpedMoving;
    cv::warpAffine(movingMask, warpedMoving, affine, fixedMask.size(),
                   cv::INTER_NEAREST, cv::BORDER_CONSTANT, 0);

    cv::Mat intersectionMask;
    cv::bitwise_and(warpedMoving, fixedMask, intersectionMask);

    const double intersection = static_cast<double>(cv::countNonZero(intersectionMask));
    const double movingArea   = static_cast<double>(cv::countNonZero(warpedMoving));
    const double unionArea    = movingArea + precomputedFixedArea - intersection;

    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

double ComputeGradientAgreementScore(const cv::Mat& movingImage,
                                     const cv::Mat& fixedNorm,
                                     const Transform2D& transform)
{
    cv::Mat affine = (cv::Mat_<double>(2, 3) <<
        transform.matrix[0], transform.matrix[1], transform.matrix[2],
        transform.matrix[3], transform.matrix[4], transform.matrix[5]);

    cv::Mat warpedMoving;
    cv::warpAffine(movingImage, warpedMoving, affine, fixedNorm.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    cv::Mat movingGray;
    cv::cvtColor(warpedMoving, movingGray, cv::COLOR_BGR2GRAY);

    cv::Mat movingGradX, movingGradY;
    cv::Sobel(movingGray, movingGradX, CV_32F, 1, 0, 3);
    cv::Sobel(movingGray, movingGradY, CV_32F, 0, 1, 3);
    cv::Mat movingMagnitude;
    cv::magnitude(movingGradX, movingGradY, movingMagnitude);
    cv::Mat movingNorm;
    cv::normalize(movingMagnitude, movingNorm, 0.0, 1.0, cv::NORM_MINMAX);

    const double sampleCount      = movingNorm.total() > 0 ? static_cast<double>(movingNorm.total()) : 1.0;
    const double gradientDistance = cv::norm(movingNorm, fixedNorm, cv::NORM_L1) / sampleCount;
    return (std::max)(0.0, 1.0 - gradientDistance);
}

// Threshold below which gradient computation is skipped (candidate has negligible mask overlap)
constexpr double kEarlyExitMaskThreshold = 0.05;

double ComputeCombinedScore(const LevelData& level, const Transform2D& transform)
{
    const double maskScore = ComputeMaskOverlapScore(
        level.movingMask, level.fixedMask, level.fixedArea, transform);

    // Early exit: candidate with negligible overlap cannot beat any reasonable incumbent
    if (maskScore < kEarlyExitMaskThreshold)
        return maskScore * 0.7;

    const double gradientScore = ComputeGradientAgreementScore(
        level.movingImage, level.fixedNorm, transform);

    return maskScore * 0.7 + gradientScore * 0.3;
}

// ============================================================
// Refinement — pyramid levels + parallel candidate evaluation
// ============================================================

void RefineParameters(const std::array<LevelData, 3>& levels,
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
        int    levelIndex; // index into levels[]: 0=quarter, 1=half, 2=full
    };

    constexpr double kPi = 3.14159265358979323846;
    const std::array<StepConfig, 3> steps {{
        {16.0, 16.0, 6.0 * kPi / 180.0, 0.08,  0},   // Coarse — quarter resolution
        { 8.0,  8.0, 3.0 * kPi / 180.0, 0.04,  1},   // Medium — half resolution
        { 3.0,  3.0, 1.0 * kPi / 180.0, 0.015, 2}    // Fine   — full resolution
    }};

    struct Candidate
    {
        double scale;
        double theta;
        double tx;
        double ty;
        double score = -1.0;
    };

    for (const StepConfig& step : steps)
    {
        const LevelData& level = levels[static_cast<std::size_t>(step.levelIndex)];

        bool improved = true;
        while (improved)
        {
            improved = false;

            // Build the complete set of neighbours (3^4 - 1 = 80 candidates)
            std::vector<Candidate> candidates;
            candidates.reserve(80);

            for (int thetaOffset = -1; thetaOffset <= 1; ++thetaOffset)
            {
                for (int scaleOffset = -1; scaleOffset <= 1; ++scaleOffset)
                {
                    for (int tyOffset = -1; tyOffset <= 1; ++tyOffset)
                    {
                        for (int txOffset = -1; txOffset <= 1; ++txOffset)
                        {
                            if (thetaOffset == 0 && scaleOffset == 0 &&
                                tyOffset    == 0 && txOffset    == 0)
                                continue;

                            candidates.push_back({
                                (std::max)(0.1, scale + step.scaleStep * static_cast<double>(scaleOffset)),
                                NormalizeAngle(theta  + step.thetaStep * static_cast<double>(thetaOffset)),
                                tx + step.txStep * static_cast<double>(txOffset),
                                ty + step.tyStep * static_cast<double>(tyOffset),
                                -1.0
                            });
                        }
                    }
                }
            }

            // Score all candidates in parallel — each operates on its own local Mats
            std::for_each(std::execution::par_unseq, candidates.begin(), candidates.end(),
                [&level](Candidate& c)
                {
                    // tx/ty are in original-image space; scale them for this pyramid level
                    const Transform2D t = BuildSimilarityTransform(
                        c.scale, c.theta,
                        c.tx * level.coordScale,
                        c.ty * level.coordScale);
                    c.score = ComputeCombinedScore(level, t);
                });

            // Pick the best candidate
            const auto best = std::max_element(
                candidates.begin(), candidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

            if (best != candidates.end() && best->score > bestScore)
            {
                scale     = best->scale;
                theta     = best->theta;
                tx        = best->tx;
                ty        = best->ty;
                bestScore = best->score;
                improved  = true;
                iterations.push_back(
                    {static_cast<int>(iterations.size()), bestScore, tx, ty, theta, scale, false});
            }
        }
    }
}

// ============================================================
// Shared setup for both public entry points
// ============================================================

std::array<LevelData, 3> BuildPyramid(const cv::Mat& movingMask,
                                      const cv::Mat& fixedMask,
                                      const cv::Mat& movingImage,
                                      const cv::Mat& fixedImage)
{
    return {
        BuildLevelData(movingMask, fixedMask, movingImage, fixedImage, 4),
        BuildLevelData(movingMask, fixedMask, movingImage, fixedImage, 2),
        BuildLevelData(movingMask, fixedMask, movingImage, fixedImage, 1),
    };
}

} // namespace

// ============================================================
// Public API
// ============================================================

Result RegistrationEngine::RegisterCtToPhoto(const cv::Mat& movingImage,
                                             const cv::Mat& fixedImage,
                                             RegistrationResult& result) const
{
    if (movingImage.empty() || fixedImage.empty())
        return Result{false, "Images must be loaded before registration."};

    const cv::Mat movingMask = ExtractCtMask(movingImage);
    const cv::Mat fixedMask  = ExtractPhotoMask(fixedImage);

    MaskStats movingStats;
    MaskStats fixedStats;
    if (!ComputeMaskStats(movingMask, movingStats) || !ComputeMaskStats(fixedMask, fixedStats))
        return Result{false, "Could not extract a usable body mask for registration."};

    double scale = 1.0;
    double theta = 0.0;
    double tx    = 0.0;
    double ty    = 0.0;
    ComputeInitialGuess(movingStats, fixedStats, scale, theta, tx, ty);

    const std::array<LevelData, 3> levels = BuildPyramid(movingMask, fixedMask, movingImage, fixedImage);

    result.transformType = "similarity";
    result.iterations.clear();

    result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
    double bestScore = ComputeCombinedScore(levels[2], result.forward);
    result.iterations.push_back({0, bestScore, tx, ty, theta, scale, false});

    RefineParameters(levels, scale, theta, tx, ty, result.iterations, bestScore);

    result.forward   = BuildSimilarityTransform(scale, theta, tx, ty);
    result.inverse   = InvertSimilarityTransform(scale, theta, tx, ty);
    result.score     = bestScore;
    result.converged = true;

    if (!result.iterations.empty())
        result.iterations.back().converged = true;

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
        return Result{false, "Images must be loaded before refinement."};

    const cv::Mat movingMask = ExtractCtMask(movingImage);
    const cv::Mat fixedMask  = ExtractPhotoMask(fixedImage);

    result.transformType = "similarity_prior_refined";
    result.iterations.clear();

    double scale = priorScale;
    double theta = priorTheta;
    double tx    = priorTx;
    double ty    = priorTy;

    const std::array<LevelData, 3> levels = BuildPyramid(movingMask, fixedMask, movingImage, fixedImage);

    result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
    double bestScore = ComputeCombinedScore(levels[2], result.forward);
    result.iterations.push_back({0, bestScore, tx, ty, theta, scale, false});

    RefineParameters(levels, scale, theta, tx, ty, result.iterations, bestScore);

    result.forward           = BuildSimilarityTransform(scale, theta, tx, ty);
    result.inverse           = InvertSimilarityTransform(scale, theta, tx, ty);
    result.score             = bestScore;
    result.converged         = true;
    result.refinedWithPrior  = true;
    result.hasConvergencePrior = true;
    result.priorTx           = priorTx;
    result.priorTy           = priorTy;
    result.priorTheta        = priorTheta;
    result.priorScale        = priorScale;

    if (!result.iterations.empty())
        result.iterations.back().converged = true;

    return Result{};
}
} // namespace align
