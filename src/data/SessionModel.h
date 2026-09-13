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
    ConvergenceAnalysis,
    LandmarkInterpolation,
    AnimatedExport
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

struct AnimatedExportSettings
{
    int fps = 10;
    int startPairIndex = 0;
    int endPairIndex = -1;  // -1 means "last valid pair"
    bool useCurrentPreviewMode = true;
    float blendAlpha = 0.5f;
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
    std::vector<StackModel> stacks;
    std::vector<PairingModel> pairings;
    std::string activePairingId;
    std::vector<RegistrationResult> registrations;
    std::vector<AlignmentOperation> operations;
    int nextOperationId = 1;
    UiPreferences uiPreferences;
    ProjectPreferences projectPreferences;
    AnimatedExportSettings animatedExport;
};

SessionModel CreateDefaultSession();
void RebuildPairs(SessionModel& session, PairingModel& pairing);

// Resolution helpers used pervasively by the UI layer to reach "the fixed/moving stack of
// whichever pairing is currently focused" instead of two hardcoded named stacks.
StackModel* FindStack(SessionModel& session, const StackId& id);
const StackModel* FindStack(const SessionModel& session, const StackId& id);
PairingModel& GetActivePairing(SessionModel& session);
const PairingModel& GetActivePairing(const SessionModel& session);
StackModel& GetActiveFixedStack(SessionModel& session);
const StackModel& GetActiveFixedStack(const SessionModel& session);
StackModel& GetActiveMovingStack(SessionModel& session);
const StackModel& GetActiveMovingStack(const SessionModel& session);
} // namespace align
