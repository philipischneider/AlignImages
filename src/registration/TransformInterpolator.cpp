#include "registration/TransformInterpolator.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <numbers>

namespace align
{
namespace
{

// An anchor is a slice where the user placed explicit landmarks.
// Propagated results (isManual = true but landmarks cleared) and auto-registrations
// are NOT anchors — they will be overwritten by interpolation.
bool IsAnchorRegistration(const RegistrationResult& reg)
{
    return reg.isManual && !reg.landmarks.empty() && !reg.isInterpolated;
}

void ExtractSimilarityParams(const RegistrationResult& reg,
                             double& tx, double& ty, double& theta, double& scale)
{
    if (!reg.iterations.empty())
    {
        const IterationRecord& it = reg.iterations.back();
        tx    = it.tx;
        ty    = it.ty;
        theta = it.theta;
        scale = it.scale > 0.0 ? it.scale : 1.0;
        return;
    }

    tx    = reg.forward.matrix[2];
    ty    = reg.forward.matrix[5];
    scale = std::sqrt(reg.forward.matrix[0] * reg.forward.matrix[0] +
                      reg.forward.matrix[3] * reg.forward.matrix[3]);
    scale = scale > 0.0 ? scale : 1.0;
    theta = std::atan2(reg.forward.matrix[3], reg.forward.matrix[0]);
}

double LerpAngle(double a, double b, double t)
{
    double diff = b - a;
    constexpr double kPi = std::numbers::pi;
    while (diff >  kPi) diff -= 2.0 * kPi;
    while (diff < -kPi) diff += 2.0 * kPi;
    return a + diff * t;
}

Transform2D BuildSimilarityForward(double tx, double ty, double theta, double scale)
{
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    Transform2D fwd;
    fwd.matrix[0] = scale * c;  fwd.matrix[1] = -scale * s;  fwd.matrix[2] = tx;
    fwd.matrix[3] = scale * s;  fwd.matrix[4] =  scale * c;  fwd.matrix[5] = ty;
    fwd.matrix[6] = 0.0;        fwd.matrix[7] = 0.0;          fwd.matrix[8] = 1.0;
    return fwd;
}

Transform2D InvertAffine(const Transform2D& fwd)
{
    const cv::Mat full = (cv::Mat_<double>(3, 3) <<
        fwd.matrix[0], fwd.matrix[1], fwd.matrix[2],
        fwd.matrix[3], fwd.matrix[4], fwd.matrix[5],
        0.0, 0.0, 1.0);
    const cv::Mat inv = full.inv();
    Transform2D result;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            result.matrix[r * 3 + c] = inv.at<double>(r, c);
    return result;
}

} // namespace

int ApplyTransformInterpolation(const std::vector<PairRecord>& pairs,
                                std::vector<RegistrationResult>& registrations,
                                const StackId& fixedStackId,
                                const StackId& movingStackId)
{
    struct Anchor
    {
        int       pairIndex;
        SliceIndex fixedIndex;
        SliceIndex movingIndex;
        double tx, ty, theta, scale;
    };

    std::vector<Anchor> anchors;
    anchors.reserve(registrations.size());

    for (int i = 0; i < static_cast<int>(pairs.size()); ++i)
    {
        const PairRecord& pair = pairs[i];
        if (!pair.valid)
            continue;

        const RegistrationResult* reg =
            FindRegistrationResult(registrations, fixedStackId, movingStackId, pair.fixedIndex, pair.movingIndex);
        if (reg == nullptr || !IsAnchorRegistration(*reg))
            continue;

        Anchor a;
        a.pairIndex  = i;
        a.fixedIndex  = pair.fixedIndex;
        a.movingIndex = pair.movingIndex;
        ExtractSimilarityParams(*reg, a.tx, a.ty, a.theta, a.scale);
        anchors.push_back(a);
    }

    if (anchors.size() < 2)
        return 0;

    int count = 0;
    for (size_t ai = 0; ai + 1 < anchors.size(); ++ai)
    {
        const Anchor& startA = anchors[ai];
        const Anchor& endA   = anchors[ai + 1];
        const int gap = endA.pairIndex - startA.pairIndex;
        if (gap <= 1)
            continue;

        for (int i = startA.pairIndex + 1; i < endA.pairIndex; ++i)
        {
            const PairRecord& pair = pairs[i];
            if (!pair.valid)
                continue;

            RegistrationResult* existing =
                FindRegistrationResult(registrations, fixedStackId, movingStackId, pair.fixedIndex, pair.movingIndex);

            // Protect only slices with explicit user-placed landmarks.
            if (existing != nullptr && existing->isManual && !existing->landmarks.empty() && !existing->isInterpolated)
                continue;

            const double t     = static_cast<double>(i - startA.pairIndex) / static_cast<double>(gap);
            const double tx    = startA.tx    + (endA.tx    - startA.tx)    * t;
            const double ty    = startA.ty    + (endA.ty    - startA.ty)    * t;
            const double scale = startA.scale + (endA.scale - startA.scale) * t;
            const double theta = LerpAngle(startA.theta, endA.theta, t);

            const Transform2D fwd = BuildSimilarityForward(tx, ty, theta, scale);
            const Transform2D inv = InvertAffine(fwd);

            RegistrationResult interp;
            interp.fixedStackId  = fixedStackId;
            interp.movingStackId = movingStackId;
            interp.fixedIndex    = pair.fixedIndex;
            interp.movingIndex   = pair.movingIndex;
            interp.forward       = fwd;
            interp.inverse       = inv;
            interp.transformType = "interpolated";
            interp.converged     = true;
            interp.isInterpolated = true;
            interp.score         = 0.0;
            interp.iterations.push_back({0, 0.0, tx, ty, theta, scale, true});

            if (existing != nullptr)
            {
                interp.history = existing->history;
                interp.operationLog = existing->operationLog;
                *existing = std::move(interp);
            }
            else
            {
                registrations.push_back(std::move(interp));
            }
            ++count;
        }
    }

    return count;
}

} // namespace align
