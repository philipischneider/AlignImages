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
    // Affine mode: sx and sy are independent scale factors (>0).
    // -1.0 means not set — interpret scale as sx == sy (similarity mode).
    double sx = -1.0;
    double sy = -1.0;
    bool converged = false;
};

struct RegistrationSnapshot
{
    int operationId = 0;
    std::string label;
    std::string timestamp;
    std::string transformType = "similarity";
    Transform2D forward;
    Transform2D inverse;
    double score = 0.0;
    double manualRmsError = 0.0;
    bool isManual = false;
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
    double priorTxMean = 0.0;
    double priorTyMean = 0.0;
    double priorThetaMean = 0.0;
    double priorScaleMean = 1.0;
    double priorTxStdDev = -1.0;
    double priorTyStdDev = -1.0;
    double priorThetaStdDev = -1.0;
    double priorScaleStdDev = -1.0;
    // Affine priors — set by ConvergenceAnalyzer when sx/sy data is available.
    // -1.0 means not set; fall back to priorScale for both axes.
    double priorSx = -1.0;
    double priorSy = -1.0;
    double priorSxMean = -1.0;
    double priorSyMean = -1.0;
    double priorSxStdDev = -1.0;
    double priorSyStdDev = -1.0;
    // Audit trail
    std::string timestamp;         // ISO 8601 (local time) of the last operation, e.g. "2026-04-12T14:30:00"
    std::string algorithmVersion;  // e.g. "similarity_v1", "affine_v1", "manual_landmarks"
    std::vector<std::string> operationLog; // chronological list, e.g. "2026-04-12T14:30 initial_auto score=0.71"
    std::vector<RegistrationSnapshot> history;
    std::vector<IterationRecord> iterations;
    std::vector<LandmarkPair> landmarks;
};

RegistrationResult* FindRegistrationResult(std::vector<RegistrationResult>& registrations,
                                           SliceIndex fixedIndex,
                                           SliceIndex movingIndex);
const RegistrationResult* FindRegistrationResult(const std::vector<RegistrationResult>& registrations,
                                                 SliceIndex fixedIndex,
                                                 SliceIndex movingIndex);
void AppendHistorySnapshot(RegistrationResult& registration, const std::string& label);
} // namespace align
