#pragma once

#include "data/RegistrationResult.h"

#include <vector>

namespace align
{
class ConvergenceAnalyzer
{
public:
    void Analyze(std::vector<RegistrationResult>& registrations, bool preferManualPriors = true) const;
};
} // namespace align
