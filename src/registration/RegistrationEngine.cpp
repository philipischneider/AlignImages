#include "registration/RegistrationEngine.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <execution>
#include <sstream>
#include <vector>

namespace align
{
namespace
{

// ============================================================
// Timestamp helper
// ============================================================

std::string GetTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string FormatLogEntry(const std::string& ts, const std::string& operation, double score)
{
    std::ostringstream oss;
    oss << ts << " " << operation << " score=" << std::to_string(score).substr(0, 6);
    return oss.str();
}

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

// Affine similarity with independent X/Y scale (scaled rotation, no shear).
// Matrix:  [ sx*cos(θ)  -sy*sin(θ)  tx ]
//          [ sx*sin(θ)   sy*cos(θ)  ty ]
Transform2D BuildAffineTransform(double sx, double sy, double theta, double tx, double ty)
{
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    Transform2D t;
    t.matrix = {
        sx * c, -sy * s, tx,
        sx * s,  sy * c, ty,
        0.0, 0.0, 1.0
    };
    return t;
}

Transform2D InvertAffineTransform(double sx, double sy, double theta, double tx, double ty)
{
    const double safeSx = std::abs(sx) < 1e-6 ? 1.0 : sx;
    const double safeSy = std::abs(sy) < 1e-6 ? 1.0 : sy;
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    // Inverse of [sx*c, -sy*s; sx*s, sy*c] is [c/sx, s/sx; -s/sy, c/sy]
    const double txInv = -(c * tx + s * ty) / safeSx;
    const double tyInv =  (s * tx - c * ty) / safeSy;
    Transform2D t;
    t.matrix = {
         c / safeSx,  s / safeSx, txInv,
        -s / safeSy,  c / safeSy, tyInv,
        0.0, 0.0, 1.0
    };
    return t;
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

// Affine initial guess: independent bounding-box scale per axis.
// Falls back to area-based uniform scale when the axis ratio is too close to 1.
void ComputeInitialGuessAffine(const MaskStats& movingStats,
                                const MaskStats& fixedStats,
                                double& sx,
                                double& sy,
                                double& theta,
                                double& tx,
                                double& ty)
{
    const double movingW = movingStats.bounds.width  > 0 ? static_cast<double>(movingStats.bounds.width)  : 1.0;
    const double movingH = movingStats.bounds.height > 0 ? static_cast<double>(movingStats.bounds.height) : 1.0;
    const double fixedW  = fixedStats.bounds.width   > 0 ? static_cast<double>(fixedStats.bounds.width)   : 1.0;
    const double fixedH  = fixedStats.bounds.height  > 0 ? static_cast<double>(fixedStats.bounds.height)  : 1.0;

    const double sxBB = std::clamp(fixedW / movingW, 0.3, 3.5);
    const double syBB = std::clamp(fixedH / movingH, 0.3, 3.5);
    const double ratio = (std::max)(sxBB, syBB) / (std::max)((std::min)(sxBB, syBB), 0.01);

    if (ratio < 1.15)
    {
        // Nearly uniform — use area-based uniform scale for stability
        const double movingSize = (std::max)(movingW, movingH);
        const double fixedSize  = (std::max)(fixedW,  fixedH);
        sx = sy = std::clamp(fixedSize / movingSize, 0.3, 3.5);
    }
    else
    {
        sx = sxBB;
        sy = syBB;
    }

    theta = NormalizeAngle(fixedStats.angle - movingStats.angle);
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    tx = fixedStats.center.x - (sx * c * movingStats.center.x - sy * s * movingStats.center.y);
    ty = fixedStats.center.y - (sx * s * movingStats.center.x + sy * c * movingStats.center.y);
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

struct SimilarityBounds
{
    bool enabled = false;
    double minScale = 0.1;
    double maxScale = 10.0;
    double minTheta = -3.14159265358979323846;
    double maxTheta = 3.14159265358979323846;
    double minTx = -1.0e9;
    double maxTx = 1.0e9;
    double minTy = -1.0e9;
    double maxTy = 1.0e9;
};

struct AffineBounds
{
    bool enabled = false;
    double minSx = 0.1;
    double maxSx = 10.0;
    double minSy = 0.1;
    double maxSy = 10.0;
    double minTheta = -3.14159265358979323846;
    double maxTheta = 3.14159265358979323846;
    double minTx = -1.0e9;
    double maxTx = 1.0e9;
    double minTy = -1.0e9;
    double maxTy = 1.0e9;
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
// Mutual information — self-contained, downsamples internally so it's cheap enough to
// call every frame for a live readout (see MUTUAL_INFORMATION_PLAN.md).
// ============================================================

// Longest-side cap for the working resolution; keeps histogram building fast enough to run
// once per rendered frame while the user drags landmarks or the Transpose gizmo.
constexpr int kMiDownsampleMaxDim = 220;
constexpr int kMiHistogramBins = 32;

double ComputeMutualInformationScoreImpl(const cv::Mat& movingImageBgr, const cv::Mat& fixedImageBgr, const Transform2D& transform)
{
    if (movingImageBgr.empty() || fixedImageBgr.empty())
    {
        return 0.0;
    }

    const int longestSide = (std::max)(fixedImageBgr.cols, fixedImageBgr.rows);
    const double scale = longestSide > kMiDownsampleMaxDim
                             ? static_cast<double>(kMiDownsampleMaxDim) / static_cast<double>(longestSide)
                             : 1.0;
    const cv::Size workingSize((std::max)(1, static_cast<int>(fixedImageBgr.cols * scale)),
                               (std::max)(1, static_cast<int>(fixedImageBgr.rows * scale)));

    cv::Mat fixedGray;
    cv::cvtColor(fixedImageBgr, fixedGray, cv::COLOR_BGR2GRAY);
    cv::Mat fixedSmall;
    cv::resize(fixedGray, fixedSmall, workingSize, 0, 0, cv::INTER_AREA);

    cv::Mat movingGray;
    cv::cvtColor(movingImageBgr, movingGray, cv::COLOR_BGR2GRAY);

    // `movingGray` is left at full resolution (warpAffine's cost is driven by the destination
    // size, not the source size, so there's no need to also downsample it) while the destination
    // canvas is the small `workingSize`. The map from full-res moving coordinates to
    // small-canvas fixed coordinates is fixedSmall = scale*(A*moving + t) = (scale*A)*moving +
    // scale*t -- i.e. the WHOLE affine matrix scales uniformly, not just the translation. Scaling
    // only the translation (as an earlier version of this function did) leaves the linear part
    // too "large" for the small destination, so warpAffine ends up sampling a tiny corner of the
    // full-resolution moving image instead of the whole (equivalently shrunk) frame -- comparing
    // unrelated pixels and pinning MI near 0 regardless of actual alignment quality.
    Transform2D scaledTransform = transform;
    scaledTransform.matrix[0] *= scale;
    scaledTransform.matrix[1] *= scale;
    scaledTransform.matrix[2] *= scale;
    scaledTransform.matrix[3] *= scale;
    scaledTransform.matrix[4] *= scale;
    scaledTransform.matrix[5] *= scale;

    const cv::Mat affine = (cv::Mat_<double>(2, 3) <<
        scaledTransform.matrix[0], scaledTransform.matrix[1], scaledTransform.matrix[2],
        scaledTransform.matrix[3], scaledTransform.matrix[4], scaledTransform.matrix[5]);

    cv::Mat warpedMoving;
    cv::warpAffine(movingGray, warpedMoving, affine, workingSize, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

    // Validity mask from the warp's own support region (not from pixel intensity): a
    // pixel-value-based "exclude black" filter would wrongly discard real anatomy whenever the
    // moving image's Window/Level maps most of the tissue near 0 (e.g. Bone/Soft Tissue presets),
    // while only a wide window like Lung happened to spread values away from 0. Warping an
    // all-white mask with the same transform/border marks exactly the pixels that fall outside
    // the moving image's original extent, regardless of what Window/Level did to their intensity.
    cv::Mat validitySource(movingGray.size(), CV_8UC1, cv::Scalar(255));
    cv::Mat validity;
    cv::warpAffine(validitySource, validity, affine, workingSize, cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));

    // Joint histogram with partial-volume interpolation on the moving-intensity axis (Maes et
    // al. 1997 / Chen & Varshney 2003) -- splits each sample's weight between its two nearest
    // moving-intensity bins by linear distance, instead of hard-rounding to one bin, to avoid
    // the periodic false-minima artifacts a nearest-neighbor joint histogram would introduce.
    std::vector<double> jointHist(static_cast<size_t>(kMiHistogramBins) * kMiHistogramBins, 0.0);
    std::vector<double> fixedHist(kMiHistogramBins, 0.0);
    std::vector<double> movingHist(kMiHistogramBins, 0.0);

    const double binScale = static_cast<double>(kMiHistogramBins) / 256.0;
    double totalWeight = 0.0;

    for (int y = 0; y < workingSize.height; ++y)
    {
        const uchar* fixedRow = fixedSmall.ptr<uchar>(y);
        const uchar* movingRow = warpedMoving.ptr<uchar>(y);
        const uchar* validityRow = validity.ptr<uchar>(y);
        for (int x = 0; x < workingSize.width; ++x)
        {
            // Only exclude pixels truly outside the moving image's warped extent -- not pixels
            // that merely windowed to a dark value, which is valid content.
            if (validityRow[x] == 0)
            {
                continue;
            }

            const uchar movingVal = movingRow[x];
            const uchar fixedVal = fixedRow[x];
            const int fBin = (std::min)(kMiHistogramBins - 1, static_cast<int>(fixedVal * binScale));
            const double mBinF = movingVal * binScale;
            const int mBin0 = (std::min)(kMiHistogramBins - 1, static_cast<int>(mBinF));
            const int mBin1 = (std::min)(kMiHistogramBins - 1, mBin0 + 1);
            const double mFrac = mBinF - mBin0;
            const double wLow = 1.0 - mFrac;
            const double wHigh = mFrac;

            jointHist[static_cast<size_t>(fBin) * kMiHistogramBins + mBin0] += wLow;
            jointHist[static_cast<size_t>(fBin) * kMiHistogramBins + mBin1] += wHigh;
            fixedHist[fBin] += 1.0;
            movingHist[mBin0] += wLow;
            movingHist[mBin1] += wHigh;
            totalWeight += 1.0;
        }
    }

    if (totalWeight < 1.0)
    {
        return 0.0;
    }

    auto entropyOf = [totalWeight](const std::vector<double>& hist)
    {
        double h = 0.0;
        for (const double count : hist)
        {
            if (count <= 0.0)
            {
                continue;
            }
            const double p = count / totalWeight;
            h -= p * std::log(p);
        }
        return h;
    };

    const double hFixed = entropyOf(fixedHist);
    const double hMoving = entropyOf(movingHist);
    const double hJoint = entropyOf(jointHist);
    const double mutualInformation = hFixed + hMoving - hJoint;

    const double denom = hFixed + hMoving;
    if (denom <= 1e-9)
    {
        return 0.0;
    }

    // Normalized mutual information: 2*I(F,R)/(H(F)+H(R)), in [0,1] like the other scores.
    return (std::clamp)(2.0 * mutualInformation / denom, 0.0, 1.0);
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
                      double& bestScore,
                      const SimilarityBounds* bounds = nullptr)
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

                            Candidate candidate {
                                (std::max)(0.1, scale + step.scaleStep * static_cast<double>(scaleOffset)),
                                NormalizeAngle(theta  + step.thetaStep * static_cast<double>(thetaOffset)),
                                tx + step.txStep * static_cast<double>(txOffset),
                                ty + step.tyStep * static_cast<double>(tyOffset),
                                -1.0
                            };
                            if (bounds != nullptr && bounds->enabled)
                            {
                                candidate.scale = std::clamp(candidate.scale, bounds->minScale, bounds->maxScale);
                                candidate.theta = std::clamp(candidate.theta, bounds->minTheta, bounds->maxTheta);
                                candidate.tx = std::clamp(candidate.tx, bounds->minTx, bounds->maxTx);
                                candidate.ty = std::clamp(candidate.ty, bounds->minTy, bounds->maxTy);
                            }
                            candidates.push_back(candidate);
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
// Affine refinement — 5D grid search (tx, ty, theta, sx, sy)
// ============================================================

void RefineParametersAffine(const std::array<LevelData, 3>& levels,
                             double& sx,
                             double& sy,
                             double& theta,
                             double& tx,
                             double& ty,
                             std::vector<IterationRecord>& iterations,
                             double& bestScore,
                             const AffineBounds* bounds = nullptr)
{
    struct StepConfig
    {
        double txStep;
        double tyStep;
        double thetaStep;
        double scaleStep;  // applied independently to sx and sy
        int    levelIndex;
    };

    constexpr double kPi = 3.14159265358979323846;
    const std::array<StepConfig, 3> steps {{
        {16.0, 16.0, 6.0 * kPi / 180.0, 0.08,  0},
        { 8.0,  8.0, 3.0 * kPi / 180.0, 0.04,  1},
        { 3.0,  3.0, 1.0 * kPi / 180.0, 0.015, 2}
    }};

    struct Candidate
    {
        double sx;
        double sy;
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

            // 3^5 - 1 = 242 candidates (tx, ty, theta, sx, sy each ∈ {-1,0,+1})
            std::vector<Candidate> candidates;
            candidates.reserve(242);

            for (int sxOffset = -1; sxOffset <= 1; ++sxOffset)
            for (int syOffset = -1; syOffset <= 1; ++syOffset)
            for (int thetaOffset = -1; thetaOffset <= 1; ++thetaOffset)
            for (int tyOffset = -1; tyOffset <= 1; ++tyOffset)
            for (int txOffset = -1; txOffset <= 1; ++txOffset)
            {
                if (sxOffset == 0 && syOffset == 0 && thetaOffset == 0 &&
                    tyOffset  == 0 && txOffset  == 0)
                    continue;

                Candidate candidate {
                    (std::max)(0.1, sx + step.scaleStep * static_cast<double>(sxOffset)),
                    (std::max)(0.1, sy + step.scaleStep * static_cast<double>(syOffset)),
                    NormalizeAngle(theta + step.thetaStep * static_cast<double>(thetaOffset)),
                    tx + step.txStep * static_cast<double>(txOffset),
                    ty + step.tyStep * static_cast<double>(tyOffset),
                    -1.0
                };
                if (bounds != nullptr && bounds->enabled)
                {
                    candidate.sx = std::clamp(candidate.sx, bounds->minSx, bounds->maxSx);
                    candidate.sy = std::clamp(candidate.sy, bounds->minSy, bounds->maxSy);
                    candidate.theta = std::clamp(candidate.theta, bounds->minTheta, bounds->maxTheta);
                    candidate.tx = std::clamp(candidate.tx, bounds->minTx, bounds->maxTx);
                    candidate.ty = std::clamp(candidate.ty, bounds->minTy, bounds->maxTy);
                }
                candidates.push_back(candidate);
            }

            std::for_each(std::execution::par_unseq, candidates.begin(), candidates.end(),
                [&level](Candidate& c)
                {
                    const Transform2D t = BuildAffineTransform(
                        c.sx, c.sy, c.theta,
                        c.tx * level.coordScale,
                        c.ty * level.coordScale);
                    c.score = ComputeCombinedScore(level, t);
                });

            const auto best = std::max_element(
                candidates.begin(), candidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

            if (best != candidates.end() && best->score > bestScore)
            {
                sx        = best->sx;
                sy        = best->sy;
                theta     = best->theta;
                tx        = best->tx;
                ty        = best->ty;
                bestScore = best->score;
                improved  = true;

                IterationRecord rec;
                rec.index     = static_cast<int>(iterations.size());
                rec.score     = bestScore;
                rec.tx        = tx;
                rec.ty        = ty;
                rec.theta     = theta;
                rec.scale     = (sx + sy) * 0.5;  // geometric mean for display
                rec.sx        = sx;
                rec.sy        = sy;
                rec.converged = false;
                iterations.push_back(rec);
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

double RegistrationEngine::ComputeMutualInformationScore(const cv::Mat& movingImage,
                                                         const cv::Mat& fixedImage,
                                                         const Transform2D& transform) const
{
    return ComputeMutualInformationScoreImpl(movingImage, fixedImage, transform);
}

Result RegistrationEngine::RegisterCtToPhoto(const cv::Mat& movingImage,
                                             const cv::Mat& fixedImage,
                                             RegistrationResult& result,
                                             bool useAffine) const
{
    if (movingImage.empty() || fixedImage.empty())
        return Result{false, "Images must be loaded before registration."};

    const cv::Mat movingMask = ExtractCtMask(movingImage);
    const cv::Mat fixedMask  = ExtractPhotoMask(fixedImage);

    MaskStats movingStats;
    MaskStats fixedStats;
    if (!ComputeMaskStats(movingMask, movingStats) || !ComputeMaskStats(fixedMask, fixedStats))
        return Result{false, "Could not extract a usable body mask for registration."};

    const std::array<LevelData, 3> levels = BuildPyramid(movingMask, fixedMask, movingImage, fixedImage);
    result.iterations.clear();

    double bestScore = 0.0;
    const std::string ts = GetTimestamp();

    if (useAffine)
    {
        double sx = 1.0, sy = 1.0, theta = 0.0, tx = 0.0, ty = 0.0;
        ComputeInitialGuessAffine(movingStats, fixedStats, sx, sy, theta, tx, ty);

        result.transformType = "affine";
        result.forward = BuildAffineTransform(sx, sy, theta, tx, ty);
        bestScore = ComputeCombinedScore(levels[2], result.forward);
        IterationRecord init;
        init.index = 0; init.score = bestScore; init.tx = tx; init.ty = ty;
        init.theta = theta; init.scale = (sx + sy) * 0.5; init.sx = sx; init.sy = sy;
        result.iterations.push_back(init);

        RefineParametersAffine(levels, sx, sy, theta, tx, ty, result.iterations, bestScore);

        result.forward = BuildAffineTransform(sx, sy, theta, tx, ty);
        result.inverse = InvertAffineTransform(sx, sy, theta, tx, ty);
    }
    else
    {
        double scale = 1.0, theta = 0.0, tx = 0.0, ty = 0.0;
        ComputeInitialGuess(movingStats, fixedStats, scale, theta, tx, ty);

        result.transformType = "similarity";
        result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
        bestScore = ComputeCombinedScore(levels[2], result.forward);
        result.iterations.push_back({0, bestScore, tx, ty, theta, scale, false});

        RefineParameters(levels, scale, theta, tx, ty, result.iterations, bestScore);

        result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
        result.inverse = InvertSimilarityTransform(scale, theta, tx, ty);
    }

    result.score     = bestScore;
    result.converged = true;
    if (!result.iterations.empty())
        result.iterations.back().converged = true;

    result.timestamp        = ts;
    result.algorithmVersion = useAffine ? "affine_v1" : "similarity_v1";
    result.operationLog.push_back(
        FormatLogEntry(ts, "initial_auto [" + result.transformType + "]", bestScore));

    return Result{};
}

Result RegistrationEngine::RefineCtToPhotoFromPrior(const cv::Mat& movingImage,
                                                    const cv::Mat& fixedImage,
                                                    double priorTx,
                                                    double priorTy,
                                                    double priorTheta,
                                                    double priorScale,
                                                    RegistrationResult& result,
                                                    bool useAffine,
                                                    double priorSx,
                                                    double priorSy,
                                                    bool useSigmaBounds,
                                                    double sigmaMultiplier,
                                                    double priorTxStdDev,
                                                    double priorTyStdDev,
                                                    double priorThetaStdDev,
                                                    double priorScaleStdDev,
                                                    double priorSxStdDev,
                                                    double priorSyStdDev) const
{
    if (movingImage.empty() || fixedImage.empty())
        return Result{false, "Images must be loaded before refinement."};

    const cv::Mat movingMask = ExtractCtMask(movingImage);
    const cv::Mat fixedMask  = ExtractPhotoMask(fixedImage);
    const std::array<LevelData, 3> levels = BuildPyramid(movingMask, fixedMask, movingImage, fixedImage);
    result.iterations.clear();

    double bestScore = 0.0;
    const std::string ts = GetTimestamp();
    SimilarityBounds similarityBounds;
    AffineBounds affineBounds;
    if (useSigmaBounds)
    {
        similarityBounds.enabled = priorTxStdDev > 0.0 || priorTyStdDev > 0.0 ||
                                   priorThetaStdDev > 0.0 || priorScaleStdDev > 0.0;
        similarityBounds.minTx = priorTx - (priorTxStdDev > 0.0 ? sigmaMultiplier * priorTxStdDev : 0.0);
        similarityBounds.maxTx = priorTx + (priorTxStdDev > 0.0 ? sigmaMultiplier * priorTxStdDev : 0.0);
        similarityBounds.minTy = priorTy - (priorTyStdDev > 0.0 ? sigmaMultiplier * priorTyStdDev : 0.0);
        similarityBounds.maxTy = priorTy + (priorTyStdDev > 0.0 ? sigmaMultiplier * priorTyStdDev : 0.0);
        similarityBounds.minTheta = priorTheta - (priorThetaStdDev > 0.0 ? sigmaMultiplier * priorThetaStdDev : 0.0);
        similarityBounds.maxTheta = priorTheta + (priorThetaStdDev > 0.0 ? sigmaMultiplier * priorThetaStdDev : 0.0);
        similarityBounds.minScale = (std::max)(0.1, priorScale - (priorScaleStdDev > 0.0 ? sigmaMultiplier * priorScaleStdDev : 0.0));
        similarityBounds.maxScale = (std::max)(similarityBounds.minScale, priorScale +
            (priorScaleStdDev > 0.0 ? sigmaMultiplier * priorScaleStdDev : 0.0));

        affineBounds.enabled = similarityBounds.enabled || priorSxStdDev > 0.0 || priorSyStdDev > 0.0;
        affineBounds.minTx = similarityBounds.minTx;
        affineBounds.maxTx = similarityBounds.maxTx;
        affineBounds.minTy = similarityBounds.minTy;
        affineBounds.maxTy = similarityBounds.maxTy;
        affineBounds.minTheta = similarityBounds.minTheta;
        affineBounds.maxTheta = similarityBounds.maxTheta;
        const double baseSx = priorSx > 0.0 ? priorSx : priorScale;
        const double baseSy = priorSy > 0.0 ? priorSy : priorScale;
        affineBounds.minSx = (std::max)(0.1, baseSx - (priorSxStdDev > 0.0 ? sigmaMultiplier * priorSxStdDev : 0.0));
        affineBounds.maxSx = (std::max)(affineBounds.minSx, baseSx +
            (priorSxStdDev > 0.0 ? sigmaMultiplier * priorSxStdDev : 0.0));
        affineBounds.minSy = (std::max)(0.1, baseSy - (priorSyStdDev > 0.0 ? sigmaMultiplier * priorSyStdDev : 0.0));
        affineBounds.maxSy = (std::max)(affineBounds.minSy, baseSy +
            (priorSyStdDev > 0.0 ? sigmaMultiplier * priorSyStdDev : 0.0));
    }

    if (useAffine)
    {
        // Resolve affine priors — fall back to priorScale if affine priors not available
        const double startSx = priorSx > 0.0 ? priorSx : priorScale;
        const double startSy = priorSy > 0.0 ? priorSy : priorScale;
        double sx = startSx, sy = startSy, theta = priorTheta, tx = priorTx, ty = priorTy;

        result.transformType = "affine_prior_refined";
        result.forward = BuildAffineTransform(sx, sy, theta, tx, ty);
        bestScore = ComputeCombinedScore(levels[2], result.forward);
        IterationRecord init;
        init.index = 0; init.score = bestScore; init.tx = tx; init.ty = ty;
        init.theta = theta; init.scale = (sx + sy) * 0.5; init.sx = sx; init.sy = sy;
        result.iterations.push_back(init);

        RefineParametersAffine(levels, sx, sy, theta, tx, ty, result.iterations, bestScore, &affineBounds);

        result.forward = BuildAffineTransform(sx, sy, theta, tx, ty);
        result.inverse = InvertAffineTransform(sx, sy, theta, tx, ty);
    }
    else
    {
        double scale = priorScale, theta = priorTheta, tx = priorTx, ty = priorTy;

        result.transformType = "similarity_prior_refined";
        result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
        bestScore = ComputeCombinedScore(levels[2], result.forward);
        result.iterations.push_back({0, bestScore, tx, ty, theta, scale, false});

        RefineParameters(levels, scale, theta, tx, ty, result.iterations, bestScore, &similarityBounds);

        result.forward = BuildSimilarityTransform(scale, theta, tx, ty);
        result.inverse = InvertSimilarityTransform(scale, theta, tx, ty);
    }

    result.score              = bestScore;
    result.converged          = true;
    result.refinedWithPrior   = true;
    result.hasConvergencePrior = true;
    result.priorTx            = priorTx;
    result.priorTy            = priorTy;
    result.priorTheta         = priorTheta;
    result.priorScale         = priorScale;

    if (!result.iterations.empty())
        result.iterations.back().converged = true;

    result.timestamp        = ts;
    result.algorithmVersion = useAffine ? "affine_v1" : "similarity_v1";
    result.operationLog.push_back(
        FormatLogEntry(ts, "prior_refined [" + result.transformType + "]", bestScore));

    return Result{};
}
} // namespace align
