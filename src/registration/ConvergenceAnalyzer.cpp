#include "registration/ConvergenceAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace align
{
namespace
{
struct Parameters
{
    double tx = 0.0;
    double ty = 0.0;
    double theta = 0.0;
    double scale = 1.0;
    double sx = -1.0;  // -1 = not available (similarity result)
    double sy = -1.0;
};

bool HasUsableRegistration(const RegistrationResult& registration)
{
    return registration.converged || registration.isManual || !registration.iterations.empty();
}

Parameters ExtractParameters(const RegistrationResult& registration)
{
    Parameters parameters;
    if (!registration.iterations.empty())
    {
        const IterationRecord& iteration = registration.iterations.back();
        parameters.tx    = iteration.tx;
        parameters.ty    = iteration.ty;
        parameters.theta = iteration.theta;
        parameters.scale = iteration.scale;
        parameters.sx    = iteration.sx;
        parameters.sy    = iteration.sy;
        return parameters;
    }

    parameters.tx = registration.forward.matrix[2];
    parameters.ty = registration.forward.matrix[5];
    parameters.scale = std::sqrt(registration.forward.matrix[0] * registration.forward.matrix[0] +
                                 registration.forward.matrix[3] * registration.forward.matrix[3]);
    parameters.theta = std::atan2(registration.forward.matrix[3], registration.forward.matrix[0]);
    return parameters;
}

double Median(std::vector<double> values)
{
    if (values.empty())
    {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 0)
    {
        return 0.5 * (values[mid - 1] + values[mid]);
    }
    return values[mid];
}

double Mean(const std::vector<double>& values)
{
    if (values.empty())
    {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values)
    {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double StdDev(const std::vector<double>& values, double mean)
{
    if (values.size() < 2)
    {
        return -1.0;
    }

    double variance = 0.0;
    for (double value : values)
    {
        const double delta = value - mean;
        variance += delta * delta;
    }
    return std::sqrt(variance / static_cast<double>(values.size()));
}
} // namespace

void ConvergenceAnalyzer::Analyze(std::vector<RegistrationResult>& registrations, bool preferManualPriors) const
{
    std::sort(registrations.begin(), registrations.end(), [](const RegistrationResult& a, const RegistrationResult& b)
    {
        return a.movingIndex < b.movingIndex;
    });

    constexpr int kWindowRadius = 2;
    std::vector<int> usableNeighbors;
    usableNeighbors.reserve(registrations.size());
    for (int i = 0; i < static_cast<int>(registrations.size()); ++i)
    {
        if (HasUsableRegistration(registrations[i]))
        {
            usableNeighbors.push_back(i);
        }
    }

    for (int i = 0; i < static_cast<int>(registrations.size()); ++i)
    {
        std::vector<int> preferredNeighbors;
        std::vector<int> fallbackNeighbors;
        std::vector<int> globalManualNeighbors;
        const int targetMovingIndex = registrations[i].movingIndex;
        for (int neighbor : usableNeighbors)
        {
            const RegistrationResult& candidate = registrations[neighbor];
            const int distance = std::abs(candidate.movingIndex - targetMovingIndex);
            if (distance <= kWindowRadius)
            {
                fallbackNeighbors.push_back(neighbor);
                if (preferManualPriors && candidate.isManual)
                {
                    preferredNeighbors.push_back(neighbor);
                }
            }

            if (preferManualPriors && candidate.isManual)
            {
                globalManualNeighbors.push_back(neighbor);
            }
        }

        if (preferManualPriors && preferredNeighbors.empty())
        {
            preferredNeighbors = globalManualNeighbors;
        }
        if (fallbackNeighbors.empty())
        {
            fallbackNeighbors = usableNeighbors;
        }

        const std::vector<int>& sourceNeighbors =
            !preferredNeighbors.empty() ? preferredNeighbors : fallbackNeighbors;

        std::vector<double> txValues;
        std::vector<double> tyValues;
        std::vector<double> thetaValues;
        std::vector<double> scaleValues;
        std::vector<double> sxValues;
        std::vector<double> syValues;
        for (int neighbor : sourceNeighbors)
        {
            const Parameters params = ExtractParameters(registrations[neighbor]);
            txValues.push_back(params.tx);
            tyValues.push_back(params.ty);
            thetaValues.push_back(params.theta);
            scaleValues.push_back(params.scale);
            if (params.sx > 0.0) sxValues.push_back(params.sx);
            if (params.sy > 0.0) syValues.push_back(params.sy);
        }

        RegistrationResult& registration = registrations[i];
        registration.hasConvergencePrior = !txValues.empty();
        if (!registration.hasConvergencePrior)
        {
            registration.priorTx = 0.0;
            registration.priorTy = 0.0;
            registration.priorTheta = 0.0;
            registration.priorScale = 1.0;
            registration.priorTxMean = 0.0;
            registration.priorTyMean = 0.0;
            registration.priorThetaMean = 0.0;
            registration.priorScaleMean = 1.0;
            registration.priorTxStdDev = -1.0;
            registration.priorTyStdDev = -1.0;
            registration.priorThetaStdDev = -1.0;
            registration.priorScaleStdDev = -1.0;
            registration.priorSx = -1.0;
            registration.priorSy = -1.0;
            registration.priorSxMean = -1.0;
            registration.priorSyMean = -1.0;
            registration.priorSxStdDev = -1.0;
            registration.priorSyStdDev = -1.0;
            registration.convergenceOutlier = false;
            continue;
        }
        registration.priorTxMean = Mean(txValues);
        registration.priorTyMean = Mean(tyValues);
        registration.priorThetaMean = Mean(thetaValues);
        registration.priorScaleMean = Mean(scaleValues);
        registration.priorTxStdDev = StdDev(txValues, registration.priorTxMean);
        registration.priorTyStdDev = StdDev(tyValues, registration.priorTyMean);
        registration.priorThetaStdDev = StdDev(thetaValues, registration.priorThetaMean);
        registration.priorScaleStdDev = StdDev(scaleValues, registration.priorScaleMean);
        registration.priorTx    = Median(txValues);
        registration.priorTy    = Median(tyValues);
        registration.priorTheta = Median(thetaValues);
        registration.priorScale = Median(scaleValues);
        // Affine priors — only set when affine data is available in the window
        registration.priorSxMean = sxValues.empty() ? -1.0 : Mean(sxValues);
        registration.priorSyMean = syValues.empty() ? -1.0 : Mean(syValues);
        registration.priorSxStdDev = sxValues.empty() ? -1.0 : StdDev(sxValues, registration.priorSxMean);
        registration.priorSyStdDev = syValues.empty() ? -1.0 : StdDev(syValues, registration.priorSyMean);
        registration.priorSx = sxValues.empty() ? -1.0 : Median(sxValues);
        registration.priorSy = syValues.empty() ? -1.0 : Median(syValues);

        const Parameters current = ExtractParameters(registration);
        const double translationDelta = std::sqrt(
            (current.tx - registration.priorTx) * (current.tx - registration.priorTx) +
            (current.ty - registration.priorTy) * (current.ty - registration.priorTy));
        const double thetaDelta = std::abs(current.theta - registration.priorTheta);
        const double scaleDelta = std::abs(current.scale - registration.priorScale);

        registration.convergenceOutlier =
            HasUsableRegistration(registration) &&
            (translationDelta > 25.0 || thetaDelta > 8.0 * 3.14159265358979323846 / 180.0 || scaleDelta > 0.12 ||
             registration.score < 0.18);
    }
}
} // namespace align
