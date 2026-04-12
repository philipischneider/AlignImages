#pragma once

#include "core/Result.h"
#include "data/RegistrationResult.h"

namespace align
{
class LandmarkRegistration
{
public:
    Result ComputeFromLandmarks(RegistrationResult& registration) const;
};
} // namespace align

