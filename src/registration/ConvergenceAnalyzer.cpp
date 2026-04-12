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
};

Parameters ExtractParameters(const RegistrationResult& registration)
{
    Parameters parameters;
    if (!registration.iterations.empty())
    {
        const IterationRecord& iteration = registration.iterations.back();
        parameters.tx = iteration.tx;
        parameters.ty = iteration.ty;
        parameters.theta = iteration.theta;
        parameters.scale = iteration.scale;
    }
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
} // namespace

void ConvergenceAnalyzer::Analyze(std::vector<RegistrationResult>& registrations) const
{
    std::sort(registrations.begin(), registrations.end(), [](const RegistrationResult& a, const RegistrationResult& b)
    {
        return a.movingIndex < b.movingIndex;
    });

    constexpr int kWindowRadius = 2;
    for (int i = 0; i < static_cast<int>(registrations.size()); ++i)
    {
        std::vector<double> txValues;
        std::vector<double> tyValues;
        std::vector<double> thetaValues;
        std::vector<double> scaleValues;

        for (int offset = -kWindowRadius; offset <= kWindowRadius; ++offset)
        {
            const int neighbor = i + offset;
            if (neighbor < 0 || neighbor >= static_cast<int>(registrations.size()))
            {
                continue;
            }

            const Parameters params = ExtractParameters(registrations[neighbor]);
            txValues.push_back(params.tx);
            tyValues.push_back(params.ty);
            thetaValues.push_back(params.theta);
            scaleValues.push_back(params.scale);
        }

        RegistrationResult& registration = registrations[i];
        registration.hasConvergencePrior = !txValues.empty();
        registration.priorTx = Median(txValues);
        registration.priorTy = Median(tyValues);
        registration.priorTheta = Median(thetaValues);
        registration.priorScale = Median(scaleValues);

        const Parameters current = ExtractParameters(registration);
        const double translationDelta = std::sqrt(
            (current.tx - registration.priorTx) * (current.tx - registration.priorTx) +
            (current.ty - registration.priorTy) * (current.ty - registration.priorTy));
        const double thetaDelta = std::abs(current.theta - registration.priorTheta);
        const double scaleDelta = std::abs(current.scale - registration.priorScale);

        registration.convergenceOutlier =
            translationDelta > 25.0 || thetaDelta > 8.0 * 3.14159265358979323846 / 180.0 || scaleDelta > 0.12 ||
            registration.score < 0.18;
    }
}
} // namespace align
