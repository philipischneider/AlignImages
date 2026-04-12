#pragma once

#include "data/PairingModel.h"
#include "data/RegistrationResult.h"
#include "data/StackModel.h"

#include <string>
#include <vector>

namespace align
{
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
};

struct SessionModel
{
    std::string projectName = "untitled_session";
    std::string version = "0.1.0";
    StackModel stackA;
    StackModel stackB;
    PairingModel pairing;
    std::vector<RegistrationResult> registrations;
    UiPreferences uiPreferences;
    ProjectPreferences projectPreferences;
};

SessionModel CreateDefaultSession();
void RebuildPairs(SessionModel& session);
} // namespace align
