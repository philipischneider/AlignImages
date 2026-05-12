#pragma once

#include "data/PairingModel.h"
#include "data/RegistrationResult.h"
#include "data/StackModel.h"

#include <string>
#include <vector>

namespace align
{
enum class WorkflowPhase
{
    Setup,
    InitialAlignment,
    ConvergenceAnalysis,
    PriorRefinement,
    ManualRefinement,
    Export
};

enum class DpiMode
{
    Auto,
    Manual
};

enum class PreviewMode
{
    Blend,
    Checkerboard,
    Difference,
    Multiply
};

enum class OperationKind
{
    BatchAutoAlignment,
    CurrentAutoAlignment,
    ManualLandmarks,
    PriorRefinement,
    BatchExport,
    ConvergenceAnalysis
};

enum class OperationScope
{
    Global,
    Selection,
    SinglePair
};

struct OperationPairRef
{
    int fixedIndex = -1;
    int movingIndex = -1;
};

struct AlignmentOperation
{
    int id = 0;
    std::string label;
    std::string timestamp;
    OperationKind kind = OperationKind::CurrentAutoAlignment;
    OperationScope scope = OperationScope::SinglePair;
    std::string method;
    int affectedPairs = 0;
    int improvedPairs = 0;
    int worsenedPairs = 0;
    double averageScore = 0.0;
    std::vector<OperationPairRef> pairs;
};

struct UiPreferences
{
    DpiMode dpiMode = DpiMode::Auto;
    float dpiOverride = 1.5f;
    PreviewMode previewMode = PreviewMode::Blend;
    float blendAlpha = 0.5f;
    int checkerSize = 32;
    bool syncViewports = true;
    float timelineHeight = 260.0f;
};

struct ProjectPreferences
{
    std::string registrationPreset = "ct_photo_initial";
    std::string autoAlignmentMethod = "mask_centroid_orientation";
    std::string maskMethod = "modality_specific_body_mask";
    std::string scoreMethod = "mask_overlap_plus_gradient";
    std::string refinementMethod = "local_similarity_search";
    std::string transformType = "similarity";
    int maxIterations = 40;
    int coarseLevels = 3;
    int activeSliceA = 0;
    int activeSliceB = 0;
    bool useAlignmentPreview = true;
    bool useSigmaRestrictedPriorRefinement = true;
    float sigmaMultiplier = 1.5f;
    bool preferManualPriors = true;
};

struct SessionModel
{
    std::string projectName = "untitled_session";
    std::string version = "0.1.0";
    WorkflowPhase workflowPhase = WorkflowPhase::Setup;
    StackModel stackA;
    StackModel stackB;
    PairingModel pairing;
    std::vector<RegistrationResult> registrations;
    std::vector<AlignmentOperation> operations;
    int nextOperationId = 1;
    UiPreferences uiPreferences;
    ProjectPreferences projectPreferences;
};

SessionModel CreateDefaultSession();
void RebuildPairs(SessionModel& session);
} // namespace align
