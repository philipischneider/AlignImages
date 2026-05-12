#pragma once

#include "data/PairingModel.h"
#include "data/RegistrationResult.h"

#include <vector>

namespace align
{
// Fills interpolated RegistrationResult entries for pairs that fall between two
// consecutive pairs with usable (converged or manual) registrations.
// Skips intermediate pairs that already have a real (non-interpolated) anchor.
// Returns the number of results created or overwritten.
int ApplyTransformInterpolation(const std::vector<PairRecord>& pairs,
                                std::vector<RegistrationResult>& registrations);
} // namespace align
