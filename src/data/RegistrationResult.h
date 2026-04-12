#pragma once

#include "core/Transform2D.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace align
{
struct LandmarkPair
{
    double movingX = 0.0;
    double movingY = 0.0;
    double fixedX = 0.0;
    double fixedY = 0.0;
};

struct IterationRecord
{
    int index = 0;
    double score = 0.0;
    double tx = 0.0;
    double ty = 0.0;
    double theta = 0.0;
    double scale = 1.0;
    bool converged = false;
};

struct RegistrationResult
{
    SliceIndex fixedIndex = -1;
    SliceIndex movingIndex = -1;
    Transform2D forward;
    Transform2D inverse;
    std::string transformType = "similarity";
    double score = 0.0;
    double manualRmsError = 0.0;
    bool converged = false;
    bool isManual = false;
    bool hasConvergencePrior = false;
    bool convergenceOutlier = false;
    bool refinedWithPrior = false;
    double priorTx = 0.0;
    double priorTy = 0.0;
    double priorTheta = 0.0;
    double priorScale = 1.0;
    std::vector<IterationRecord> iterations;
    std::vector<LandmarkPair> landmarks;
};

RegistrationResult* FindRegistrationResult(std::vector<RegistrationResult>& registrations,
                                           SliceIndex fixedIndex,
                                           SliceIndex movingIndex);
const RegistrationResult* FindRegistrationResult(const std::vector<RegistrationResult>& registrations,
                                                 SliceIndex fixedIndex,
                                                 SliceIndex movingIndex);
} // namespace align
