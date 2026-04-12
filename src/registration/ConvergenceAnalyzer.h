#pragma once

#include "data/RegistrationResult.h"

#include <vector>

namespace align
{
class ConvergenceAnalyzer
{
public:
    void Analyze(std::vector<RegistrationResult>& registrations) const;
};
} // namespace align

