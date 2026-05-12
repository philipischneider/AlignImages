#include "ui/MainWindow.h"

#include "core/WinDialogUtils.h"
#include "core/DpiUtilsWin.h"

#include "imgui.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <future>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>

namespace align
{
namespace
{
constexpr const char* kRegistrationPresetLabels[] = {
    "CT -> Photo",
    "MRI -> Photo",
    "Film Scan -> Photo",
    "Mask -> Image",
    "Custom"
};

constexpr const char* kRegistrationPresetValues[] = {
    "ct_photo_initial",
    "mri_photo_initial",
    "film_photo_initial",
    "mask_image_initial",
    "custom"
};

constexpr const char* kAutoMethodLabels[] = {
    "Mask Centroid + Orientation",
    "Landmarks Only",
    "Prior Refinement Only"
};

constexpr const char* kAutoMethodValues[] = {
    "mask_centroid_orientation",
    "landmarks_only",
    "prior_refinement_only"
};

constexpr const char* kMaskMethodLabels[] = {
    "Modality-specific Body Mask",
    "Blue Background Rejection",
    "CT Threshold Body Mask",
    "External Mask Only"
};

constexpr const char* kMaskMethodValues[] = {
    "modality_specific_body_mask",
    "blue_background_rejection",
    "ct_threshold_body_mask",
    "external_mask_only"
};

constexpr const char* kScoreMethodLabels[] = {
    "Mask Overlap + Gradient",
    "Mask Overlap Only",
    "Gradient Only"
};

constexpr const char* kScoreMethodValues[] = {
    "mask_overlap_plus_gradient",
    "mask_overlap_only",
    "gradient_only"
};

constexpr const char* kRefinementMethodLabels[] = {
    "Local Similarity Search",
    "No Refinement",
    "Prior-guided Refinement"
};

constexpr const char* kRefinementMethodValues[] = {
    "local_similarity_search",
    "none",
    "prior_guided"
};

bool InputTextString(const char* label, std::string& value)
{
    std::array<char, 256> buffer {};
    const size_t copyLength = (std::min)(value.size(), buffer.size() - 1);
    value.copy(buffer.data(), copyLength);
    buffer[copyLength] = '\0';

    if (ImGui::InputText(label, buffer.data(), buffer.size()))
    {
        value = buffer.data();
        return true;
    }

    return false;
}

int FindRegistrationPresetIndex(const std::string& preset)
{
    for (int i = 0; i < IM_ARRAYSIZE(kRegistrationPresetValues); ++i)
    {
        if (preset == kRegistrationPresetValues[i])
        {
            return i;
        }
    }

    return IM_ARRAYSIZE(kRegistrationPresetValues) - 1;
}

int FindIndexForValue(const std::string& value, const char* const values[], int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (value == values[i])
        {
            return i;
        }
    }

    return 0;
}

void ApplyPresetDefaults(ProjectPreferences& preferences)
{
    if (preferences.registrationPreset == "ct_photo_initial")
    {
        preferences.autoAlignmentMethod = "mask_centroid_orientation";
        preferences.maskMethod = "modality_specific_body_mask";
        preferences.scoreMethod = "mask_overlap_plus_gradient";
        preferences.refinementMethod = "local_similarity_search";
        preferences.transformType = "similarity";
        return;
    }

    if (preferences.registrationPreset == "mri_photo_initial")
    {
        preferences.autoAlignmentMethod = "mask_centroid_orientation";
        preferences.maskMethod = "modality_specific_body_mask";
        preferences.scoreMethod = "gradient_only";
        preferences.refinementMethod = "local_similarity_search";
        preferences.transformType = "similarity";
        return;
    }

    if (preferences.registrationPreset == "film_photo_initial")
    {
        preferences.autoAlignmentMethod = "mask_centroid_orientation";
        preferences.maskMethod = "blue_background_rejection";
        preferences.scoreMethod = "mask_overlap_plus_gradient";
        preferences.refinementMethod = "local_similarity_search";
        preferences.transformType = "similarity";
        return;
    }

    if (preferences.registrationPreset == "mask_image_initial")
    {
        preferences.autoAlignmentMethod = "mask_centroid_orientation";
        preferences.maskMethod = "external_mask_only";
        preferences.scoreMethod = "mask_overlap_only";
        preferences.refinementMethod = "none";
        preferences.transformType = "similarity";
        return;
    }
}

const char* DescribeCurrentAutomaticPipeline(const ProjectPreferences& preferences)
{
    if (preferences.autoAlignmentMethod == "mask_centroid_orientation")
    {
        return "Current automatic pipeline: builds body masks, estimates centroid/orientation, then refines a similarity transform.";
    }
    if (preferences.autoAlignmentMethod == "landmarks_only")
    {
        return "Current automatic pipeline: disabled in favor of manual landmarks only.";
    }
    if (preferences.autoAlignmentMethod == "prior_refinement_only")
    {
        return "Current automatic pipeline: starts from prior/convergence values and only performs guided refinement.";
    }

    return "Current automatic pipeline: custom configuration.";
}

const char* PreviewModeLabel(PreviewMode mode)
{
    switch (mode)
    {
    case PreviewMode::Blend:
        return "Blend";
    case PreviewMode::Checkerboard:
        return "Checkerboard";
    case PreviewMode::Difference:
        return "Difference";
    case PreviewMode::Multiply:
        return "Multiply";
    default:
        return "Blend";
    }
}

void ShowHoveredHelp(const char* description)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && description != nullptr)
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void TextWithHelp(const char* label, const char* description)
{
    ImGui::TextUnformatted(label);
    ShowHoveredHelp(description);
}

std::string BuildSnapshotLabel(const RegistrationResult& registration)
{
    if (!registration.timestamp.empty())
    {
        return registration.timestamp + "  " + registration.transformType;
    }

    return registration.transformType.empty() ? "operation" : registration.transformType;
}

std::string BuildTimestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
    localtime_s(&tm, &t);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

const char* OperationKindLabel(OperationKind kind)
{
    switch (kind)
    {
    case OperationKind::BatchAutoAlignment:   return "Batch Auto";
    case OperationKind::CurrentAutoAlignment: return "Current Auto";
    case OperationKind::ManualLandmarks:      return "Manual Landmarks";
    case OperationKind::PriorRefinement:      return "Prior Refinement";
    case OperationKind::BatchExport:           return "Batch Export";
    case OperationKind::ConvergenceAnalysis:   return "Convergence";
    case OperationKind::LandmarkInterpolation: return "Interpolation";
    case OperationKind::AnimatedExport:        return "Animated Export";
    default:                                   return "Operation";
    }
}

int AppendOperation(SessionModel& session,
                    OperationKind kind,
                    OperationScope scope,
                    std::string label,
                    std::string method,
                    const std::vector<OperationPairRef>& pairs,
                    double averageScore = 0.0,
                    int improvedPairs = 0,
                    int worsenedPairs = 0)
{
    AlignmentOperation operation;
    operation.id = session.nextOperationId++;
    operation.kind = kind;
    operation.scope = scope;
    operation.label = std::move(label);
    operation.timestamp = BuildTimestampNow();
    operation.method = std::move(method);
    operation.pairs = pairs;
    operation.affectedPairs = static_cast<int>(pairs.size());
    operation.averageScore = averageScore;
    operation.improvedPairs = improvedPairs;
    operation.worsenedPairs = worsenedPairs;
    session.operations.push_back(std::move(operation));
    return session.operations.back().id;
}

const PairRecord* GetSelectedPair(const AppContext& context)
{
    const int activeSliceA = context.session.projectPreferences.activeSliceA;
    if (activeSliceA < 0 || activeSliceA >= static_cast<int>(context.session.pairing.pairs.size()))
    {
        return nullptr;
    }

    const PairRecord& pair = context.session.pairing.pairs[activeSliceA];
    return pair.valid ? &pair : nullptr;
}

const RegistrationResult* FindSelectedRegistration(const AppContext& context)
{
    const PairRecord* pair = GetSelectedPair(context);
    if (pair == nullptr)
    {
        return nullptr;
    }

    return FindRegistrationResult(context.session.registrations, pair->fixedIndex, pair->movingIndex);
}

bool HasUsableRegistration(const RegistrationResult& registration)
{
    return registration.converged || registration.isManual || !registration.iterations.empty();
}

int EnsureRegistrationsForValidPairs(const std::vector<PairRecord>& pairs,
                                     std::vector<RegistrationResult>& registrations)
{
    int created = 0;
    for (const PairRecord& pair : pairs)
    {
        if (!pair.valid)
        {
            continue;
        }

        if (FindRegistrationResult(registrations, pair.fixedIndex, pair.movingIndex) != nullptr)
        {
            continue;
        }

        RegistrationResult createdRegistration;
        createdRegistration.fixedIndex = pair.fixedIndex;
        createdRegistration.movingIndex = pair.movingIndex;
        registrations.push_back(createdRegistration);
        ++created;
    }

    return created;
}
} // namespace

void MainWindow::Draw(AppContext& context, GLFWwindow* window)
{
    ProcessAsyncTasks(context);
    ProcessBatchStep(context);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("MainWorkspace", nullptr, flags);
    ImGui::PopStyleVar(2);

    DrawMenuBar(context);

    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float totalHeight = ImGui::GetContentRegionAvail().y;
    const float leftWidth = totalWidth * 0.22f;
    const float rightWidth = totalWidth * 0.20f;
    const float centerWidth = totalWidth - leftWidth - rightWidth - 16.0f;
    constexpr float splitterHeight = 8.0f;
    const float minTimelineHeight = 150.0f;
    const float maxTimelineHeight = (std::max)(180.0f, totalHeight * 0.55f);
    context.session.uiPreferences.timelineHeight =
        (std::clamp)(context.session.uiPreferences.timelineHeight, minTimelineHeight, maxTimelineHeight);
    const float upperHeight = totalHeight - context.session.uiPreferences.timelineHeight - splitterHeight;

    ImGui::BeginChild("LeftPanel", ImVec2(leftWidth, upperHeight), true);
    DrawLeftPanel(context, window);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("CenterPanel", ImVec2(centerWidth, upperHeight), false);
    DrawCenterPanel(context);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RightPanel", ImVec2(rightWidth, upperHeight), true);
    DrawRightPanel(context);
    ImGui::EndChild();

    ImGui::InvisibleButton("TimelineSplitter", ImVec2(-1.0f, splitterHeight));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive())
    {
        context.session.uiPreferences.timelineHeight =
            (std::clamp)(context.session.uiPreferences.timelineHeight - ImGui::GetIO().MouseDelta.y,
                         minTimelineHeight,
                         maxTimelineHeight);
    }

    m_timelinePanel.Draw(context, context.session.uiPreferences.timelineHeight);

    ImGui::End();
}

void MainWindow::DrawMenuBar(AppContext& context)
{
    if (!ImGui::BeginMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Load Session"))
        {
            LoadSession(context);
        }
        if (ImGui::MenuItem("Save Session"))
        {
            const auto savePath = ShowSaveFileDialog(L"Save Session", L"json", L"JSON Files", L"*.json",
                                                     L"session_autosave.json");
            if (savePath.has_value())
            {
                const Result result = m_serializer.Save(context.session, *savePath);
                m_lastMessage = result.ok ? "Session saved to " + savePath->string() : result.message;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("ImGui Demo", nullptr, &context.showDemoWindow);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void MainWindow::DrawLeftPanel(AppContext& context, GLFWwindow* window)
{
    // ---- Workflow phase banner ----
    static const ImVec4 kPhaseColors[] = {
        {0.55f, 0.55f, 0.55f, 1.0f},  // Setup
        {0.25f, 0.60f, 1.00f, 1.0f},  // Initial Alignment
        {0.65f, 0.35f, 1.00f, 1.0f},  // Convergence Analysis
        {1.00f, 0.65f, 0.10f, 1.0f},  // Prior Refinement
        {1.00f, 0.35f, 0.55f, 1.0f},  // Manual Refinement
        {0.20f, 0.80f, 0.35f, 1.0f},  // Export
    };
    static const char* kPhaseLabels[] = {
        "Setup",
        "Initial Alignment",
        "Convergence Analysis",
        "Prior Refinement",
        "Manual Refinement",
        "Export",
    };
    const int phaseIdx = static_cast<int>(context.session.workflowPhase);
    ImGui::TextColored(kPhaseColors[phaseIdx], "[ %s ]", kPhaseLabels[phaseIdx]);
    ImGui::Separator();

    ImGui::TextUnformatted("Project");
    InputTextString("Session Name", context.session.projectName);
    ImGui::Separator();

    ImGui::TextUnformatted("Stacks");
    ImGui::TextWrapped("Stack A is always the reference stack. Stack B is always aligned onto Stack A.");
    DrawStackLoader(context, context.session.stackA);
    DrawStackLoader(context, context.session.stackB);

    ImGui::Separator();
    ImGui::TextUnformatted("Registration");
    int presetIndex = FindRegistrationPresetIndex(context.session.projectPreferences.registrationPreset);
    if (ImGui::Combo("Preset", &presetIndex, kRegistrationPresetLabels, IM_ARRAYSIZE(kRegistrationPresetLabels)))
    {
        context.session.projectPreferences.registrationPreset = kRegistrationPresetValues[presetIndex];
        ApplyPresetDefaults(context.session.projectPreferences);
    }
    int autoMethodIndex = FindIndexForValue(context.session.projectPreferences.autoAlignmentMethod,
                                            kAutoMethodValues,
                                            IM_ARRAYSIZE(kAutoMethodValues));
    if (ImGui::Combo("Auto Method", &autoMethodIndex, kAutoMethodLabels, IM_ARRAYSIZE(kAutoMethodLabels)))
    {
        context.session.projectPreferences.autoAlignmentMethod = kAutoMethodValues[autoMethodIndex];
    }
    int maskMethodIndex = FindIndexForValue(context.session.projectPreferences.maskMethod,
                                            kMaskMethodValues,
                                            IM_ARRAYSIZE(kMaskMethodValues));
    if (ImGui::Combo("Mask Strategy", &maskMethodIndex, kMaskMethodLabels, IM_ARRAYSIZE(kMaskMethodLabels)))
    {
        context.session.projectPreferences.maskMethod = kMaskMethodValues[maskMethodIndex];
    }
    int scoreMethodIndex = FindIndexForValue(context.session.projectPreferences.scoreMethod,
                                             kScoreMethodValues,
                                             IM_ARRAYSIZE(kScoreMethodValues));
    if (ImGui::Combo("Score Strategy", &scoreMethodIndex, kScoreMethodLabels, IM_ARRAYSIZE(kScoreMethodLabels)))
    {
        context.session.projectPreferences.scoreMethod = kScoreMethodValues[scoreMethodIndex];
    }
    int refinementMethodIndex = FindIndexForValue(context.session.projectPreferences.refinementMethod,
                                                  kRefinementMethodValues,
                                                  IM_ARRAYSIZE(kRefinementMethodValues));
    if (ImGui::Combo("Refinement", &refinementMethodIndex, kRefinementMethodLabels, IM_ARRAYSIZE(kRefinementMethodLabels)))
    {
        context.session.projectPreferences.refinementMethod = kRefinementMethodValues[refinementMethodIndex];
    }
    // Transform type — radio buttons instead of freeform text
    {
        const bool isSimilarity = context.session.projectPreferences.transformType != "affine";
        if (ImGui::RadioButton("Similarity", isSimilarity))
            context.session.projectPreferences.transformType = "similarity";
        ImGui::SameLine();
        if (ImGui::RadioButton("Affine", !isSimilarity))
            context.session.projectPreferences.transformType = "affine";
        if (!isSimilarity)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                               "Affine: independent X/Y scale. Use when modalities have different aspect ratios.");
    }
    ImGui::TextWrapped("%s", DescribeCurrentAutomaticPipeline(context.session.projectPreferences));
    ImGui::SliderInt("Max Iterations", &context.session.projectPreferences.maxIterations, 1, 250);
    ImGui::SliderInt("Coarse Levels", &context.session.projectPreferences.coarseLevels, 1, 6);
    ImGui::Checkbox("Use Alignment In Preview", &context.session.projectPreferences.useAlignmentPreview);
    ShowHoveredHelp("When enabled, the preview viewer composites the moving image after applying the current alignment or selected history snapshot.");
    ImGui::Checkbox("Sigma-Restricted Prior", &context.session.projectPreferences.useSigmaRestrictedPriorRefinement);
    ShowHoveredHelp("Restricts prior-guided refinement to a local envelope defined by prior +/- sigma multiplier times local standard deviation.");
    ImGui::SliderFloat("Sigma Multiplier", &context.session.projectPreferences.sigmaMultiplier, 0.5f, 3.0f, "%.2f");
    ShowHoveredHelp("Controls how tightly the search is constrained around local prior statistics.");
    ImGui::Checkbox("Prefer Manual Priors", &context.session.projectPreferences.preferManualPriors);
    ShowHoveredHelp("When manual landmark-aligned slices exist nearby, use them preferentially to compute local priors.");
    TextWithHelp("Registration Actions", "Run a single alignment, process the full stack, export results, or enter manual landmark editing.");
    if (!m_currentAlignmentTask.has_value() && ImGui::Button("Run Current Alignment"))
    {
        RunCurrentAlignment(context);
    }
    else if (m_currentAlignmentTask.has_value())
    {
        ImGui::BeginDisabled();
        ImGui::Button("Run Current Alignment");
        ImGui::EndDisabled();
    }
    ShowHoveredHelp("Runs the active pair in the background so the interface stays responsive.");
    ImGui::SameLine();
    if (!context.batchProcessState.running && ImGui::Button("Run Batch Alignment"))
    {
        RunBatchAlignment(context);
    }
    else if (context.batchProcessState.running)
    {
        ImGui::BeginDisabled();
        ImGui::Button("Run Batch Alignment");
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Export Current"))
    {
        ExportCurrentAligned(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Batch"))
    {
        ExportBatchAligned(context);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Transform Interpolation");
    ShowHoveredHelp("Fills slices between landmark/auto-registration anchors with linearly interpolated transforms. Only gaps without a real result are filled.");
    if (ImGui::Button("Interpolate Between Anchors"))
    {
        RunTransformInterpolation(context);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Animated Preview Export");
    ShowHoveredHelp("Renders a video where each frame is the blending preview for one slice pair. Pairs without a transform show the raw images.");
    {
        AnimatedExportSettings& anim = context.session.animatedExport;
        const int maxPair = static_cast<int>(context.session.pairing.pairs.size()) - 1;
        if (anim.endPairIndex < 0 || anim.endPairIndex > maxPair)
            anim.endPairIndex = maxPair;
        ImGui::SliderInt("Start Pair##anim", &anim.startPairIndex, 0, (std::max)(0, maxPair));
        ImGui::SliderInt("End Pair##anim",   &anim.endPairIndex,   0, (std::max)(0, maxPair));
        if (anim.startPairIndex > anim.endPairIndex)
            anim.startPairIndex = anim.endPairIndex;
        ImGui::SliderInt("FPS##anim", &anim.fps, 1, 60);
        ImGui::Checkbox("Use current preview settings##anim", &anim.useCurrentPreviewMode);
        if (!anim.useCurrentPreviewMode)
            ImGui::SliderFloat("Blend Alpha##anim", &anim.blendAlpha, 0.0f, 1.0f, "%.2f");

        if (m_animatedExportTask.has_value())
        {
            if (ImGui::Button("Cancel Animated Export"))
            {
                CancelAnimatedExport();
            }
            if (m_animatedExportProgress != nullptr)
            {
                const int attempted = m_animatedExportProgress->attempted.load();
                const int total     = m_animatedExportProgress->total.load();
                const float frac    = total > 0 ? static_cast<float>(attempted) / static_cast<float>(total) : 0.0f;
                ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
                ImGui::Text("Frames: %d / %d", attempted, total);
                std::string status;
                {
                    std::scoped_lock lock(m_animatedExportProgress->statusMutex);
                    status = m_animatedExportProgress->statusMessage;
                }
                if (!status.empty())
                    ImGui::TextWrapped("%s", status.c_str());
            }
        }
        else
        {
            if (ImGui::Button("Export Animated Preview"))
            {
                ExportAnimatedPreview(context);
            }
        }
    }

    if (ImGui::Button(context.landmarkModeEnabled ? "Disable Landmark Mode" : "Enable Landmark Mode"))
    {
        context.landmarkModeEnabled = !context.landmarkModeEnabled;
        if (!context.landmarkModeEnabled)
        {
            context.pendingLandmarkPoint.hasMovingPoint = false;
            context.landmarkEditState.selectedIndex = -1;
            context.landmarkEditState.target = LandmarkEditTarget::None;
            context.landmarkEditState.isDragging = false;
            m_lastMessage = "Landmark mode disabled.";
        }
        else
        {
            context.session.projectPreferences.useAlignmentPreview = true;
            m_lastMessage = "Landmark mode enabled. Click a point in Stack B, then the matching point in Stack A.";
        }
    }
    if (context.landmarkModeEnabled)
    {
        ImGui::TextWrapped("Landmark mode is active. The preview updates in real time as soon as at least 2 pairs exist.");
    }
    if (ImGui::Button("Analyze Convergence"))
    {
        AnalyzeConvergence(context);
    }
    ImGui::SameLine();
    if (m_priorRefinementTask.has_value())
    {
        if (ImGui::Button("Cancel Refinement"))
        {
            CancelPriorRefinement();
        }
        if (m_priorRefinementProgress != nullptr)
        {
            const int attempted = m_priorRefinementProgress->attempted.load();
            const int total     = m_priorRefinementProgress->total.load();
            const float frac    = total > 0 ? static_cast<float>(attempted) / static_cast<float>(total) : 0.0f;
            ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
            ImGui::Text("Refinement: %d / %d", attempted, total);
            std::string status;
            {
                std::scoped_lock lock(m_priorRefinementProgress->statusMutex);
                status = m_priorRefinementProgress->statusMessage;
            }
            if (!status.empty())
            {
                ImGui::TextWrapped("%s", status.c_str());
            }
        }
    }
    else
    {
        if (ImGui::Button("Run Prior Refinement"))
        {
            RunPriorRefinement(context);
        }
    }
    if (m_exportBatchTask.has_value())
    {
        if (ImGui::Button("Cancel Export"))
        {
            CancelExportBatch();
        }
        if (m_exportBatchProgress != nullptr)
        {
            const int attempted = m_exportBatchProgress->attempted.load();
            const int total     = m_exportBatchProgress->total.load();
            const float frac    = total > 0 ? static_cast<float>(attempted) / static_cast<float>(total) : 0.0f;
            ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
            ImGui::Text("Export: %d / %d", attempted, total);
            std::string status;
            {
                std::scoped_lock lock(m_exportBatchProgress->statusMutex);
                status = m_exportBatchProgress->statusMessage;
            }
            if (!status.empty())
            {
                ImGui::TextWrapped("%s", status.c_str());
            }
        }
    }
    if (context.batchProcessState.running)
    {
        if (ImGui::Button("Cancel Batch"))
        {
            CancelBatchAlignment(context);
        }
        const float progress = context.batchProcessState.totalValidPairs > 0
                                   ? static_cast<float>(context.batchProcessState.attempted) /
                                         static_cast<float>(context.batchProcessState.totalValidPairs)
                                   : 0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        ImGui::Text("Batch Progress: %d attempted, %d ok, %d failed",
                    context.batchProcessState.attempted,
                    context.batchProcessState.succeeded,
                    context.batchProcessState.failed);
        if (!context.batchProcessState.statusMessage.empty())
        {
            ImGui::TextWrapped("%s", context.batchProcessState.statusMessage.c_str());
        }
    }
    if (!m_backgroundStatus.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_backgroundStatus.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("UI Scale");
    const bool isAuto = context.session.uiPreferences.dpiMode == DpiMode::Auto;
    if (ImGui::RadioButton("Auto", isAuto))
    {
        context.session.uiPreferences.dpiMode = DpiMode::Auto;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Manual", !isAuto))
    {
        context.session.uiPreferences.dpiMode = DpiMode::Manual;
    }
    ImGui::SliderFloat("Scale Override", &context.session.uiPreferences.dpiOverride, 1.0f, 2.5f, "%.2fx");
    ImGui::Text("Detected Scale: %.2fx", GetWindowDpiScale(window));

    ImGui::Separator();
    ImGui::TextUnformatted("Preview");
    const char* previewModes[] = {"Blend", "Checkerboard", "Difference", "Multiply"};
    int previewMode = static_cast<int>(context.session.uiPreferences.previewMode);
    if (ImGui::Combo("Mode", &previewMode, previewModes, IM_ARRAYSIZE(previewModes)))
    {
        context.session.uiPreferences.previewMode = static_cast<PreviewMode>(previewMode);
    }
    ImGui::SliderFloat("Blend Alpha", &context.session.uiPreferences.blendAlpha, 0.0f, 1.0f, "%.2f");
    ImGui::SliderInt("Checker Size", &context.session.uiPreferences.checkerSize, 4, 128);
    ImGui::Checkbox("Sync Viewports", &context.session.uiPreferences.syncViewports);

    ImGui::Separator();
    DrawLandmarkEditor(context);

    ImGui::Separator();
    if (!m_lastMessage.empty())
    {
        ImGui::TextWrapped("%s", m_lastMessage.c_str());
    }
}

void MainWindow::DrawRightPanel(AppContext& context)
{
    TextWithHelp("Diagnostics", "Quick summary of loaded data, pairing status, current registration quality, and stack-level metrics.");
    ImGui::Text("Stack A slices: %d", static_cast<int>(context.session.stackA.slices.size()));
    ShowHoveredHelp("Total number of reference frames currently loaded into Stack A.");
    ImGui::Text("Stack B slices: %d", static_cast<int>(context.session.stackB.slices.size()));
    ShowHoveredHelp("Total number of moving frames currently loaded into Stack B.");
    ImGui::Text("Global Offset: %d", context.session.pairing.globalOffset);
    ShowHoveredHelp("Pairing offset applied between Stack B and Stack A in the timeline.");
    ImGui::Text("Preview: %s", PreviewModeLabel(context.session.uiPreferences.previewMode));
    ShowHoveredHelp("Current compositing mode used by the preview viewer.");
    ImGui::Text("Auto Method: %s", context.session.projectPreferences.autoAlignmentMethod.c_str());
    ImGui::Text("Mask Strategy: %s", context.session.projectPreferences.maskMethod.c_str());
    ImGui::Text("Score Strategy: %s", context.session.projectPreferences.scoreMethod.c_str());
    ImGui::Text("Refinement: %s", context.session.projectPreferences.refinementMethod.c_str());
    ImGui::Text("Pairs tracked: %d", static_cast<int>(context.session.pairing.pairs.size()));
    const BatchSummary summary = BuildBatchSummary(context);
    ImGui::Text("Registrations: %d", static_cast<int>(context.session.registrations.size()));
    ImGui::Separator();

    TextWithHelp("Batch Summary", "Aggregate status across all valid pairs after automatic or manual processing.");
    ImGui::Text("Attempted: %d", summary.attempted);
    ShowHoveredHelp("How many valid pairs have been processed or at least assigned a status.");
    ImGui::Text("Succeeded: %d", summary.succeeded);
    ShowHoveredHelp("Pairs currently marked as aligned successfully.");
    ImGui::Text("Failed: %d", summary.failed);
    ShowHoveredHelp("Pairs currently marked as suspect or failed.");
    ImGui::Text("Batch Running: %s", context.batchProcessState.running ? "yes" : "no");
    if (summary.hasScores)
    {
        ImGui::Text("Average Score: %.4f", summary.averageScore);
        ShowHoveredHelp("Mean registration score across stored results.");
        ImGui::Text("Best Score: %.4f", summary.bestScore);
        ShowHoveredHelp("Highest registration score across stored results.");
        ImGui::Text("Worst Score: %.4f", summary.worstScore);
        ShowHoveredHelp("Lowest registration score across stored results.");
    }
    ImGui::Separator();

    TextWithHelp("Current Selection", "The pair currently selected in the shared timeline.");
    ImGui::Text("Reference Slice A: %d", context.session.projectPreferences.activeSliceA);
    ImGui::Text("Moving Slice B: %d", context.session.projectPreferences.activeSliceB);
    ImGui::Separator();

    TextWithHelp("Registration Status", "State of the active pair, including transform type, score, manual status, and convergence-derived flags.");
    const RegistrationResult* registration = FindSelectedRegistration(context);
    if (registration == nullptr)
    {
        ImGui::TextWrapped("No registration has been computed for the current pair.");
    }
    else
    {
        ImGui::Text("Transform: %s", registration->transformType.c_str());
        ImGui::Text("Score: %.4f", registration->score);
        ImGui::Text("Converged: %s", registration->converged ? "yes" : "no");
        ImGui::Text("Manual: %s", registration->isManual ? "yes" : "no");
        ImGui::Text("Landmarks: %d", static_cast<int>(registration->landmarks.size()));
        ImGui::Text("Convergence Prior: %s", registration->hasConvergencePrior ? "yes" : "no");
        ImGui::Text("Convergence Outlier: %s", registration->convergenceOutlier ? "yes" : "no");
        ImGui::Text("Refined With Prior: %s", registration->refinedWithPrior ? "yes" : "no");
        if (registration->hasConvergencePrior)
        {
            ImGui::Text("Prior sigma tx/ty: %.2f / %.2f",
                        registration->priorTxStdDev,
                        registration->priorTyStdDev);
            ShowHoveredHelp("Local standard deviation used to constrain prior-guided translation search.");
            ImGui::Text("Prior sigma theta/scale: %.4f / %.4f",
                        registration->priorThetaStdDev,
                        registration->priorScaleStdDev);
            ShowHoveredHelp("Local standard deviation used to constrain prior-guided rotation and scale search.");
        }
        if (registration->isManual)
        {
            ImGui::Text("RMS Error: %.4f px", registration->manualRmsError);
        }
        if (!registration->iterations.empty())
        {
            const IterationRecord& iteration = registration->iterations.back();
            ImGui::Text("tx: %.2f px", iteration.tx);
            ImGui::Text("ty: %.2f px", iteration.ty);
            ImGui::Text("theta: %.3f deg", iteration.theta * 180.0 / 3.14159265358979323846);
            if (iteration.sx > 0.0 && iteration.sy > 0.0)
            {
                ImGui::Text("sx: %.4f  sy: %.4f", iteration.sx, iteration.sy);
            }
            else
            {
                ImGui::Text("scale: %.4f", iteration.scale);
            }
        }
    }

    ImGui::Separator();
    DrawOperationStack(context);

    ImGui::Separator();
    DrawOperationHistory(context);

    ImGui::Separator();
    DrawMetricsGraph(context);
}

void SetBatchStatus(const std::shared_ptr<MainWindow::BatchTaskProgress>& progress, const std::string& message)
{
    if (!progress)
    {
        return;
    }

    std::scoped_lock lock(progress->statusMutex);
    progress->statusMessage = message;
}

void MainWindow::ProcessBatchStep(AppContext& context)
{
    using namespace std::chrono_literals;

    BatchProcessState& batch = context.batchProcessState;
    if (!m_batchTask.has_value())
    {
        return;
    }

    if (m_batchTaskProgress != nullptr)
    {
        batch.running = true;
        batch.cancelRequested = m_batchTaskProgress->cancelRequested.load();
        batch.attempted = m_batchTaskProgress->attempted.load();
        batch.succeeded = m_batchTaskProgress->succeeded.load();
        batch.failed = m_batchTaskProgress->failed.load();
        batch.totalValidPairs = m_batchTaskProgress->totalValidPairs.load();
        batch.currentFixedIndex = m_batchTaskProgress->currentFixedIndex.load();
        batch.currentMovingIndex = m_batchTaskProgress->currentMovingIndex.load();
        {
            std::scoped_lock lock(m_batchTaskProgress->statusMutex);
            batch.statusMessage = m_batchTaskProgress->statusMessage;
        }
    }

    if (m_batchTask->wait_for(0ms) != std::future_status::ready)
    {
        return;
    }

    BatchTaskResult result = m_batchTask->get();
    m_batchTask.reset();
    m_batchTaskProgress.reset();

    batch.running = false;
    batch.cancelRequested = false;
    batch.statusMessage.clear();
    batch.attempted = result.attempted;
    batch.succeeded = result.succeeded;
    batch.failed = result.failed;

    if (!result.result.ok)
    {
        m_lastMessage = result.result.message;
        return;
    }

    context.session.pairing.pairs = std::move(result.pairs);
    std::vector<OperationPairRef> operationPairs;
    double totalScore = 0.0;
    int scoreCount = 0;
    for (RegistrationResult& registration : result.registrations)
    {
        if (!HasUsableRegistration(registration))
        {
            continue;
        }
        AppendHistorySnapshot(registration, BuildSnapshotLabel(registration));
        operationPairs.push_back({registration.fixedIndex, registration.movingIndex});
        totalScore += registration.score;
        ++scoreCount;
    }
    const int operationId = AppendOperation(
        context.session,
        OperationKind::BatchAutoAlignment,
        OperationScope::Global,
        "Batch alignment",
        context.session.projectPreferences.autoAlignmentMethod,
        operationPairs,
        scoreCount > 0 ? totalScore / static_cast<double>(scoreCount) : 0.0);
    for (RegistrationResult& registration : result.registrations)
    {
        if (!registration.history.empty())
        {
            registration.history.back().operationId = operationId;
        }
    }
    context.session.registrations = std::move(result.registrations);
    if (result.cancelled)
    {
        m_lastMessage = "Batch alignment cancelled. Processed: " + std::to_string(result.attempted) + ".";
    }
    else
    {
        m_lastMessage = "Batch alignment finished. Succeeded: " + std::to_string(result.succeeded) +
                        " / " + std::to_string(result.attempted) + ".";
    }
}

void MainWindow::DrawCenterPanel(AppContext& context)
{
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float viewerWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    const float viewerHeight = ImGui::GetContentRegionAvail().y;

    m_referenceViewer.Draw(context, ImVec2(viewerWidth, viewerHeight));
    ImGui::SameLine();
    m_movingViewer.Draw(context, ImVec2(viewerWidth, viewerHeight));
    ImGui::SameLine();
    m_previewViewer.Draw(context, ImVec2(viewerWidth, viewerHeight));
}

void MainWindow::DrawStackLoader(AppContext& context, StackModel& stack)
{
    ImGui::PushID(stack.id.c_str());
    InputTextString("Name", stack.name);
    InputTextString("Modality", stack.modality);
    InputTextString("Directory", stack.directory);
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        const auto selectedFolder = ShowSelectFolderDialog(L"Select Image Stack Folder");
        if (selectedFolder.has_value())
        {
            stack.directory = selectedFolder->string();
        }
    }
    if (ImGui::Button("Load Directory"))
    {
        LoadStack(context, stack);
    }
    ImGui::SameLine();
    ImGui::Text("%d slices", static_cast<int>(stack.slices.size()));
    ImGui::PopID();
}

void MainWindow::LoadStack(AppContext& /*context*/, StackModel& stack)
{
    if (stack.directory.empty())
    {
        m_lastMessage = "Select a directory path before loading the stack.";
        return;
    }
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before loading another stack.";
        return;
    }

    const std::filesystem::path directory = stack.directory;
    const std::string stackId = stack.id;
    const std::string stackName = stack.name;
    const std::string modality = stack.modality;
    m_backgroundStatus = "Loading image list for " + stack.name + "...";
    m_stackLoadTask = std::async(std::launch::async,
                                 [this, directory, stackId, stackName, modality]()
                                 {
                                     StackLoadTaskResult taskResult;
                                     taskResult.stackId = stackId;
                                     taskResult.result = m_directoryScanner.LoadStackFromDirectory(
                                         directory, stackId, stackName, modality, taskResult.stack);
                                     return taskResult;
                                 });
    m_lastMessage = "Started loading " + stack.name + ".";
}

void MainWindow::RunCurrentAlignment(AppContext& context)
{
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before starting another alignment.";
        return;
    }

    if (context.session.projectPreferences.activeSliceA < 0 ||
        context.session.projectPreferences.activeSliceA >= static_cast<int>(context.session.pairing.pairs.size()))
    {
        m_lastMessage = "The current reference slice in Stack A does not have a candidate pair.";
        return;
    }

    const PairRecord& pair = context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
    if (!pair.valid)
    {
        m_lastMessage = "The selected pair is outside the overlap between stacks.";
        return;
    }

    const SliceRecord& fixedSlice = context.session.stackA.slices[pair.fixedIndex];
    const SliceRecord& movingSlice = context.session.stackB.slices[pair.movingIndex];
    const bool useAffine = context.session.projectPreferences.transformType == "affine";
    const std::string autoMethod = context.session.projectPreferences.autoAlignmentMethod;
    const bool useSigmaBounds = context.session.projectPreferences.useSigmaRestrictedPriorRefinement;
    const double sigmaMultiplier = context.session.projectPreferences.sigmaMultiplier;
    const int pairListIndex = context.session.projectPreferences.activeSliceA;
    const std::optional<RegistrationResult> existingCopy = [&, pair]()
        -> std::optional<RegistrationResult>
    {
        RegistrationResult* existing =
            FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
        if (existing != nullptr)
        {
            return *existing;
        }
        return std::nullopt;
    }();

    m_backgroundStatus = "Running current alignment...";
    m_currentAlignmentTask = std::async(
        std::launch::async,
        [fixedSlice, movingSlice, pair, pairListIndex, autoMethod, useAffine, useSigmaBounds, sigmaMultiplier, existingCopy]()
        {
            CurrentAlignmentTaskResult taskResult;
            taskResult.pairListIndex = pairListIndex;
            taskResult.registration.fixedIndex = pair.fixedIndex;
            taskResult.registration.movingIndex = pair.movingIndex;

            ImageLoader imageLoader;
            RegistrationEngine registrationEngine;
            LandmarkRegistration landmarkRegistration;

            if (autoMethod == "landmarks_only")
            {
                if (!existingCopy.has_value() || existingCopy->landmarks.size() < 2)
                {
                    taskResult.result = Result{false, "Landmarks Only requires at least 2 landmark pairs for the current selection."};
                    return taskResult;
                }

                taskResult.registration = *existingCopy;
                taskResult.result = landmarkRegistration.ComputeFromLandmarks(taskResult.registration);
                if (taskResult.result.ok)
                {
                    taskResult.registration.isManual = true;
                    taskResult.pairStatus = PairStatus::Manual;
                    taskResult.workflowPhase = WorkflowPhase::ManualRefinement;
                }
                return taskResult;
            }

            cv::Mat movingImage;
            cv::Mat fixedImage;
            Result loadMoving = imageLoader.LoadColorImage(movingSlice.filePath, movingImage);
            Result loadFixed = imageLoader.LoadColorImage(fixedSlice.filePath, fixedImage);
            if (!loadMoving.ok || !loadFixed.ok)
            {
                taskResult.result = Result{false, "Could not load the active pair for registration."};
                return taskResult;
            }

            if (autoMethod == "prior_refinement_only")
            {
                if (!existingCopy.has_value() || !existingCopy->hasConvergencePrior)
                {
                    taskResult.result = Result{false, "Prior Refinement Only requires an existing registration with convergence prior."};
                    return taskResult;
                }

                taskResult.registration = *existingCopy;
                taskResult.result = registrationEngine.RefineCtToPhotoFromPrior(
                    movingImage, fixedImage,
                    existingCopy->priorTx, existingCopy->priorTy, existingCopy->priorTheta, existingCopy->priorScale,
                    taskResult.registration, useAffine, existingCopy->priorSx, existingCopy->priorSy,
                    useSigmaBounds,
                    sigmaMultiplier,
                    existingCopy->priorTxStdDev, existingCopy->priorTyStdDev,
                    existingCopy->priorThetaStdDev, existingCopy->priorScaleStdDev,
                    existingCopy->priorSxStdDev, existingCopy->priorSyStdDev);
            }
            else
            {
                taskResult.result = registrationEngine.RegisterCtToPhoto(
                    movingImage, fixedImage, taskResult.registration, useAffine);
            }

            if (!taskResult.result.ok)
            {
                return taskResult;
            }

            taskResult.pairStatus = taskResult.registration.score >= 0.15 ? PairStatus::Aligned : PairStatus::Suspect;
            return taskResult;
        });

    m_lastMessage = "Current alignment started.";
}

void MainWindow::RunBatchAlignment(AppContext& context)
{
    if (context.session.stackA.slices.empty() || context.session.stackB.slices.empty())
    {
        m_lastMessage = "Load both stacks before running batch alignment.";
        return;
    }

    if (m_batchTask.has_value() || HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before starting batch alignment.";
        return;
    }

    context.batchProcessState = {};
    context.batchProcessState.running = true;
    auto progress = std::make_shared<BatchTaskProgress>();
    std::vector<PairRecord> pairs = context.session.pairing.pairs;
    std::vector<RegistrationResult> registrations = context.session.registrations;
    const std::string autoMethod = context.session.projectPreferences.autoAlignmentMethod;
    EnsureRegistrationsForValidPairs(pairs, registrations);
    if (autoMethod == "prior_refinement_only")
    {
        m_convergenceAnalyzer.Analyze(registrations, context.session.projectPreferences.preferManualPriors);
    }
    const std::vector<SliceRecord> fixedSlices = context.session.stackA.slices;
    const std::vector<SliceRecord> movingSlices = context.session.stackB.slices;
    const bool useAffine = context.session.projectPreferences.transformType == "affine";
    const bool useSigmaBounds = context.session.projectPreferences.useSigmaRestrictedPriorRefinement;
    const double sigmaMultiplier = context.session.projectPreferences.sigmaMultiplier;
    int totalValidPairs = 0;
    for (const PairRecord& pair : pairs)
    {
        if (pair.valid)
        {
            ++totalValidPairs;
        }
    }
    progress->totalValidPairs.store(totalValidPairs);
    context.batchProcessState.totalValidPairs = totalValidPairs;
    context.batchProcessState.statusMessage = "Preparing batch alignment...";
    SetBatchStatus(progress, "Preparing batch alignment...");
    m_batchTaskProgress = progress;
    context.session.workflowPhase = WorkflowPhase::InitialAlignment;
    m_batchTask = std::async(std::launch::async,
                             [pairs, registrations, fixedSlices, movingSlices, autoMethod, useAffine,
                              useSigmaBounds, sigmaMultiplier, progress]()
                             {
                                 BatchTaskResult taskResult;
                                 taskResult.pairs = pairs;
                                 taskResult.registrations = registrations;

                                 ImageLoader imageLoader;
                                 RegistrationEngine registrationEngine;
                                 LandmarkRegistration landmarkRegistration;

                                 for (PairRecord& pair : taskResult.pairs)
                                 {
                                     if (progress->cancelRequested.load())
                                     {
                                         taskResult.cancelled = true;
                                         taskResult.result = Result{};
                                         break;
                                     }

                                     if (!pair.valid)
                                     {
                                         pair.status = PairStatus::Unmatched;
                                         continue;
                                     }

                                     progress->attempted.fetch_add(1);
                                     progress->currentFixedIndex.store(pair.fixedIndex);
                                     progress->currentMovingIndex.store(pair.movingIndex);
                                     SetBatchStatus(progress,
                                                    "Processing moving B:" + std::to_string(pair.movingIndex) +
                                                        " -> reference A:" + std::to_string(pair.fixedIndex));

                                     if (pair.fixedIndex < 0 || pair.fixedIndex >= static_cast<int>(fixedSlices.size()) ||
                                         pair.movingIndex < 0 || pair.movingIndex >= static_cast<int>(movingSlices.size()))
                                     {
                                         pair.status = PairStatus::Suspect;
                                         progress->failed.fetch_add(1);
                                         continue;
                                     }

                                     const SliceRecord& fixedSlice = fixedSlices[pair.fixedIndex];
                                     const SliceRecord& movingSlice = movingSlices[pair.movingIndex];

                                     cv::Mat movingImage;
                                     cv::Mat fixedImage;
                                     Result loadMoving = imageLoader.LoadColorImage(movingSlice.filePath, movingImage);
                                     Result loadFixed = imageLoader.LoadColorImage(fixedSlice.filePath, fixedImage);
                                     if (!loadMoving.ok || !loadFixed.ok)
                                     {
                                         pair.status = PairStatus::Suspect;
                                         progress->failed.fetch_add(1);
                                         continue;
                                     }

                                     RegistrationResult computed;
                                     computed.fixedIndex = pair.fixedIndex;
                                     computed.movingIndex = pair.movingIndex;
                                     Result registration;

                                     RegistrationResult* existing =
                                         FindRegistrationResult(taskResult.registrations, pair.fixedIndex, pair.movingIndex);

                                     if (autoMethod == "prior_refinement_only")
                                     {
                                         if (existing == nullptr || !existing->hasConvergencePrior)
                                         {
                                             pair.status = PairStatus::Suspect;
                                             progress->failed.fetch_add(1);
                                             continue;
                                         }

                                         computed = *existing;
                                         registration = registrationEngine.RefineCtToPhotoFromPrior(
                                             movingImage, fixedImage,
                                             existing->priorTx, existing->priorTy,
                                             existing->priorTheta, existing->priorScale,
                                             computed, useAffine,
                                             existing->priorSx, existing->priorSy,
                                             useSigmaBounds,
                                             sigmaMultiplier,
                                             existing->priorTxStdDev, existing->priorTyStdDev,
                                             existing->priorThetaStdDev, existing->priorScaleStdDev,
                                             existing->priorSxStdDev, existing->priorSyStdDev);
                                     }
                                     else if (autoMethod == "landmarks_only")
                                     {
                                         if (existing == nullptr || existing->landmarks.size() < 2)
                                         {
                                             pair.status = PairStatus::Suspect;
                                             progress->failed.fetch_add(1);
                                             continue;
                                         }

                                         computed = *existing;
                                         registration = landmarkRegistration.ComputeFromLandmarks(computed);
                                         if (registration.ok)
                                         {
                                             computed.isManual = true;
                                         }
                                     }
                                     else
                                     {
                                         registration = registrationEngine.RegisterCtToPhoto(
                                             movingImage, fixedImage, computed, useAffine);
                                     }

                                     if (!registration.ok)
                                     {
                                         pair.status = PairStatus::Suspect;
                                         progress->failed.fetch_add(1);
                                         continue;
                                     }

                                     RegistrationResult* stored =
                                         FindRegistrationResult(taskResult.registrations, computed.fixedIndex, computed.movingIndex);
                                     if (stored != nullptr)
                                     {
                                         *stored = computed;
                                     }
                                     else
                                     {
                                         taskResult.registrations.push_back(computed);
                                     }

                                     pair.status = computed.isManual ? PairStatus::Manual
                                                                     : (computed.score >= 0.15 ? PairStatus::Aligned
                                                                                               : PairStatus::Suspect);
                                     if (pair.status == PairStatus::Aligned || pair.status == PairStatus::Manual)
                                     {
                                         progress->succeeded.fetch_add(1);
                                     }
                                     else
                                     {
                                         progress->failed.fetch_add(1);
                                     }
                                 }

                                 taskResult.attempted = progress->attempted.load();
                                 taskResult.succeeded = progress->succeeded.load();
                                 taskResult.failed = progress->failed.load();
                                 taskResult.result = Result{};
                                 SetBatchStatus(progress, taskResult.cancelled ? "Cancelling batch..." : "Finalizing batch...");
                                 return taskResult;
                             });
    m_lastMessage = "Batch alignment started.";
}

void MainWindow::CancelBatchAlignment(AppContext& context)
{
    if (!context.batchProcessState.running)
    {
        return;
    }

    context.batchProcessState.cancelRequested = true;
    context.batchProcessState.statusMessage = "Cancellation requested...";
    if (m_batchTaskProgress != nullptr)
    {
        m_batchTaskProgress->cancelRequested.store(true);
        SetBatchStatus(m_batchTaskProgress, "Cancellation requested...");
    }
}

void MainWindow::LoadSession(AppContext& context)
{
    const auto selectedFile = ShowOpenFileDialog(L"Load Session", L"JSON Files", L"*.json");
    if (!selectedFile.has_value())
    {
        return;
    }

    SessionModel loaded;
    const Result result = m_serializer.Load(*selectedFile, loaded);
    if (!result.ok)
    {
        m_lastMessage = result.message;
        return;
    }

    context.session = std::move(loaded);
    m_lastMessage = "Session loaded from " + selectedFile->string();
}

void MainWindow::ExportCurrentAligned(AppContext& context)
{
    const PairRecord* pair = nullptr;
    if (context.session.projectPreferences.activeSliceA >= 0 &&
        context.session.projectPreferences.activeSliceA < static_cast<int>(context.session.pairing.pairs.size()))
    {
        pair = &context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
    }

    if (pair == nullptr || !pair->valid)
    {
        m_lastMessage = "No valid current pair available for export.";
        return;
    }

    const RegistrationResult* registration =
        FindRegistrationResult(context.session.registrations, pair->fixedIndex, pair->movingIndex);
    if (registration == nullptr)
    {
        m_lastMessage = "Compute alignment for the current pair before exporting.";
        return;
    }

    const SliceRecord& fixedSlice = context.session.stackA.slices[pair->fixedIndex];
    const SliceRecord& movingSlice = context.session.stackB.slices[pair->movingIndex];

    const auto outputDir = ShowSelectFolderDialog(L"Select Export Folder");
    if (!outputDir.has_value())
    {
        return;
    }

    const std::filesystem::path movingToFixedPath =
        *outputDir / ("movingB_to_fixedA_B" + std::to_string(pair->movingIndex) + "_A" + std::to_string(pair->fixedIndex) + ".png");
    const std::filesystem::path fixedToMovingPath =
        *outputDir / ("fixedA_to_movingB_A" + std::to_string(pair->fixedIndex) + "_B" + std::to_string(pair->movingIndex) + ".png");

    Result exportForward = m_exportController.ExportAlignedMovingToFixed(
        movingSlice.filePath, fixedSlice.filePath, *registration, movingToFixedPath);
    Result exportInverse = m_exportController.ExportAlignedFixedToMoving(
        fixedSlice.filePath, movingSlice.filePath, *registration, fixedToMovingPath);

    if (!exportForward.ok || !exportInverse.ok)
    {
        m_lastMessage = exportForward.ok ? exportInverse.message : exportForward.message;
        return;
    }

    context.session.workflowPhase = WorkflowPhase::Export;
    m_lastMessage = "Current pair exported to " + outputDir->string();
}

void MainWindow::ExportBatchAligned(AppContext& context)
{
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before starting batch export.";
        return;
    }

    const auto outputDir = ShowSelectFolderDialog(L"Select Batch Export Folder");
    if (!outputDir.has_value())
    {
        return;
    }

    std::vector<PairRecord>        pairs         = context.session.pairing.pairs;
    std::vector<RegistrationResult> registrations = context.session.registrations;
    const std::vector<SliceRecord>  fixedSlices   = context.session.stackA.slices;
    const std::vector<SliceRecord>  movingSlices  = context.session.stackB.slices;
    const std::filesystem::path     baseDir       = *outputDir;

    int total = 0;
    for (const PairRecord& p : pairs)
    {
        if (p.valid && FindRegistrationResult(registrations, p.fixedIndex, p.movingIndex) != nullptr)
        {
            ++total;
        }
    }

    auto progress = std::make_shared<GenericTaskProgress>();
    progress->total.store(total);
    m_exportBatchProgress = progress;

    context.session.workflowPhase = WorkflowPhase::Export;
    m_backgroundStatus = "Batch export running...";

    m_exportBatchTask = std::async(
        std::launch::async,
        [pairs, registrations, fixedSlices, movingSlices, baseDir, progress]()
        {
            ExportBatchTaskResult taskResult;
            taskResult.total = static_cast<int>(pairs.size());

            ExportController exportController;

            for (const PairRecord& pair : pairs)
            {
                if (progress->cancelRequested.load())
                {
                    taskResult.cancelled = true;
                    break;
                }

                if (!pair.valid)
                {
                    continue;
                }

                const RegistrationResult* reg =
                    FindRegistrationResult(registrations, pair.fixedIndex, pair.movingIndex);
                if (reg == nullptr)
                {
                    continue;
                }

                if (pair.fixedIndex  < 0 || pair.fixedIndex  >= static_cast<int>(fixedSlices.size()) ||
                    pair.movingIndex < 0 || pair.movingIndex >= static_cast<int>(movingSlices.size()))
                {
                    continue;
                }

                {
                    std::scoped_lock lock(progress->statusMutex);
                    progress->statusMessage =
                        "Exporting B:" + std::to_string(pair.movingIndex) +
                        " -> A:" + std::to_string(pair.fixedIndex);
                }

                const std::filesystem::path movingToFixedPath =
                    baseDir / "moving_to_fixed" /
                    ("B" + std::to_string(pair.movingIndex) + "_to_A" + std::to_string(pair.fixedIndex) + ".png");
                const std::filesystem::path fixedToMovingPath =
                    baseDir / "fixed_to_moving" /
                    ("A" + std::to_string(pair.fixedIndex) + "_to_B" + std::to_string(pair.movingIndex) + ".png");

                Result fwd = exportController.ExportAlignedMovingToFixed(
                    movingSlices[pair.movingIndex].filePath,
                    fixedSlices[pair.fixedIndex].filePath,
                    *reg, movingToFixedPath);
                Result inv = exportController.ExportAlignedFixedToMoving(
                    fixedSlices[pair.fixedIndex].filePath,
                    movingSlices[pair.movingIndex].filePath,
                    *reg, fixedToMovingPath);

                progress->attempted.fetch_add(1);

                if (fwd.ok && inv.ok)
                {
                    ++taskResult.exported;
                }
            }

            return taskResult;
        });

    m_lastMessage = "Batch export started.";
}

void MainWindow::CancelExportBatch()
{
    if (m_exportBatchProgress != nullptr)
    {
        m_exportBatchProgress->cancelRequested.store(true);
        std::scoped_lock lock(m_exportBatchProgress->statusMutex);
        m_exportBatchProgress->statusMessage = "Cancellation requested...";
    }
}

void MainWindow::StoreRegistrationResult(AppContext& context, const RegistrationResult& computed, int operationId)
{
    RegistrationResult stored = computed;
    if (operationId != 0)
    {
        AppendHistorySnapshot(stored, BuildSnapshotLabel(stored));
        if (!stored.history.empty())
        {
            stored.history.back().operationId = operationId;
        }
    }

    RegistrationResult* existing =
        FindRegistrationResult(context.session.registrations, computed.fixedIndex, computed.movingIndex);
    if (existing != nullptr)
    {
        stored.history.insert(stored.history.begin(), existing->history.begin(), existing->history.end());
        *existing = std::move(stored);
    }
    else
    {
        context.session.registrations.push_back(std::move(stored));
    }
}

RegistrationResult* MainWindow::GetOrCreateCurrentRegistration(AppContext& context)
{
    if (context.session.projectPreferences.activeSliceA < 0 ||
        context.session.projectPreferences.activeSliceA >= static_cast<int>(context.session.pairing.pairs.size()))
    {
        return nullptr;
    }

    const PairRecord& pair = context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
    if (!pair.valid)
    {
        return nullptr;
    }

    RegistrationResult* existing =
        FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
    if (existing != nullptr)
    {
        return existing;
    }

    RegistrationResult created;
    created.fixedIndex = pair.fixedIndex;
    created.movingIndex = pair.movingIndex;
    context.session.registrations.push_back(created);
    return &context.session.registrations.back();
}

void MainWindow::DrawLandmarkEditor(AppContext& context)
{
    ImGui::TextUnformatted("Manual Landmarks");
    if (!context.landmarkModeEnabled)
    {
        ImGui::TextWrapped("Enable Landmark Mode to place or move points directly in Stack A and Stack B.");
    }
    RegistrationResult* registration = GetOrCreateCurrentRegistration(context);
    if (registration == nullptr)
    {
        ImGui::TextWrapped("Select a valid pair to edit landmarks.");
        return;
    }

    if (ImGui::Button("Add Landmark"))
    {
        registration->landmarks.push_back({});
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Landmarks"))
    {
        registration->landmarks.clear();
        context.landmarkEditState.selectedIndex = -1;
        context.landmarkEditState.target = LandmarkEditTarget::None;
        context.landmarkEditState.isDragging = false;
    }

    ImGui::TextWrapped("Clique primeiro na imagem movel (Stack B) e depois na referencia (Stack A) para criar um par. Clique sobre um ponto para selecionar e arrastar. Use Delete para remover o ponto selecionado.");
    if (static_cast<int>(registration->landmarks.size()) < 2)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "At least 2 landmark pairs are required.");
    }
    if (context.landmarkEditState.selectedIndex >= 0)
    {
        ImGui::Text("Selected Landmark: %d", context.landmarkEditState.selectedIndex + 1);
        if (ImGui::Button("Move Up") && context.landmarkEditState.selectedIndex > 0)
        {
            const int index = context.landmarkEditState.selectedIndex;
            std::swap(registration->landmarks[index], registration->landmarks[index - 1]);
            context.landmarkEditState.selectedIndex = index - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Move Down") &&
            context.landmarkEditState.selectedIndex < static_cast<int>(registration->landmarks.size()) - 1)
        {
            const int index = context.landmarkEditState.selectedIndex;
            std::swap(registration->landmarks[index], registration->landmarks[index + 1]);
            context.landmarkEditState.selectedIndex = index + 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Selected"))
        {
            registration->landmarks.erase(registration->landmarks.begin() + context.landmarkEditState.selectedIndex);
            context.landmarkEditState.selectedIndex = -1;
            context.landmarkEditState.target = LandmarkEditTarget::None;
            context.landmarkEditState.isDragging = false;
        }
    }

    for (int i = 0; i < static_cast<int>(registration->landmarks.size()); ++i)
    {
        LandmarkPair& landmark = registration->landmarks[i];
        ImGui::PushID(i);
        ImGui::Separator();
        ImGui::Text("Point %d", i + 1);
        ImGui::InputDouble("Moving X", &landmark.movingX, 1.0, 10.0, "%.2f");
        ImGui::InputDouble("Moving Y", &landmark.movingY, 1.0, 10.0, "%.2f");
        ImGui::InputDouble("Fixed X", &landmark.fixedX, 1.0, 10.0, "%.2f");
        ImGui::InputDouble("Fixed Y", &landmark.fixedY, 1.0, 10.0, "%.2f");
        if (ImGui::Button("Remove"))
        {
            registration->landmarks.erase(registration->landmarks.begin() + i);
            if (context.landmarkEditState.selectedIndex == i)
            {
                context.landmarkEditState.selectedIndex = -1;
                context.landmarkEditState.target = LandmarkEditTarget::None;
                context.landmarkEditState.isDragging = false;
            }
            else if (context.landmarkEditState.selectedIndex > i)
            {
                context.landmarkEditState.selectedIndex -= 1;
            }
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    const bool enoughLandmarks = static_cast<int>(registration->landmarks.size()) >= 2;
    if (!enoughLandmarks)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Apply Landmarks"))
    {
        ApplyManualLandmarks(context);
        registration = GetOrCreateCurrentRegistration(context);
        if (registration == nullptr)
            return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply to All Pairs"))
    {
        PropagateManualLandmarksToAll(context);
        registration = GetOrCreateCurrentRegistration(context);
        if (registration == nullptr)
            return;
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Apply to Interval");

    const int totalPairs = static_cast<int>(context.session.pairing.pairs.size());
    static int s_intervalFrom = 1;
    static int s_intervalTo   = 1;
    // Keep defaults in sync when the pair count changes.
    if (s_intervalTo < 1 || s_intervalTo > totalPairs)
        s_intervalTo = totalPairs;
    if (s_intervalFrom < 1)
        s_intervalFrom = 1;
    if (s_intervalFrom > s_intervalTo)
        s_intervalFrom = s_intervalTo;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("From##interval", &s_intervalFrom);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("To##interval", &s_intervalTo);
    s_intervalFrom = std::clamp(s_intervalFrom, 1, std::max(1, totalPairs));
    s_intervalTo   = std::clamp(s_intervalTo,   s_intervalFrom, std::max(1, totalPairs));

    ImGui::SameLine();
    if (ImGui::Button("Apply to Interval"))
    {
        PropagateManualLandmarksToInterval(context, s_intervalFrom - 1, s_intervalTo - 1);
        registration = GetOrCreateCurrentRegistration(context);
        if (registration == nullptr)
            return;
    }

    if (!enoughLandmarks)
    {
        ImGui::EndDisabled();
    }
    if (registration->isManual && registration->manualRmsError > 0.0)
    {
        ImGui::Text("RMS error: %.2f px", registration->manualRmsError);
    }
}

void MainWindow::ApplyManualLandmarks(AppContext& context)
{
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish.";
        return;
    }

    RegistrationResult* registration = GetOrCreateCurrentRegistration(context);
    if (registration == nullptr)
    {
        m_lastMessage = "No valid pair selected for manual alignment.";
        return;
    }

    const Result result = m_landmarkRegistration.ComputeFromLandmarks(*registration);
    if (!result.ok)
    {
        m_lastMessage = result.message;
        return;
    }

    const int operationId = AppendOperation(
        context.session,
        OperationKind::ManualLandmarks,
        OperationScope::SinglePair,
        "Apply manual landmarks",
        "manual_landmarks",
        {OperationPairRef{registration->fixedIndex, registration->movingIndex}},
        registration->score);
    StoreRegistrationResult(context, *registration, operationId);
    registration = GetOrCreateCurrentRegistration(context);

    if (context.session.projectPreferences.activeSliceA >= 0 &&
        context.session.projectPreferences.activeSliceA < static_cast<int>(context.session.pairing.pairs.size()))
    {
        PairRecord& pair = context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
        pair.status = PairStatus::Manual;
    }

    context.session.workflowPhase = WorkflowPhase::ManualRefinement;
    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, context.session.registrations);
    m_convergenceAnalyzer.Analyze(context.session.registrations, context.session.projectPreferences.preferManualPriors);

    int propagatedPairs = 0;
    for (const RegistrationResult& current : context.session.registrations)
    {
        if (current.hasConvergencePrior)
        {
            ++propagatedPairs;
        }
    }

    m_lastMessage = "Manual landmark alignment applied to the current pair. Convergence priors propagated to " +
                    std::to_string(propagatedPairs) + " stack pairs.";
}

void MainWindow::PropagateManualLandmarksToAll(AppContext& context)
{
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish.";
        return;
    }

    RegistrationResult* current = GetOrCreateCurrentRegistration(context);
    if (current == nullptr)
    {
        m_lastMessage = "No valid pair selected for landmark propagation.";
        return;
    }

    if (current->landmarks.size() < 2)
    {
        m_lastMessage = "At least 2 landmark pairs are required to propagate to all pairs.";
        return;
    }

    RegistrationResult source = *current;
    const Result computeResult = m_landmarkRegistration.ComputeFromLandmarks(source);
    if (!computeResult.ok)
    {
        m_lastMessage = computeResult.message;
        return;
    }

    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, context.session.registrations);

    std::vector<OperationPairRef> affectedPairs;
    for (const PairRecord& pair : context.session.pairing.pairs)
    {
        if (pair.valid)
        {
            affectedPairs.push_back({pair.fixedIndex, pair.movingIndex});
        }
    }

    const int operationId = AppendOperation(
        context.session,
        OperationKind::ManualLandmarks,
        OperationScope::Global,
        "Propagate manual landmarks to all pairs",
        "manual_landmarks",
        affectedPairs,
        source.score);

    for (const PairRecord& pair : context.session.pairing.pairs)
    {
        if (!pair.valid)
        {
            continue;
        }

        RegistrationResult target = source;
        target.fixedIndex = pair.fixedIndex;
        target.movingIndex = pair.movingIndex;
        if (pair.fixedIndex != source.fixedIndex || pair.movingIndex != source.movingIndex)
        {
            target.landmarks.clear();
        }
        StoreRegistrationResult(context, target, operationId);
    }

    for (PairRecord& pair : context.session.pairing.pairs)
    {
        if (pair.valid)
        {
            pair.status = PairStatus::Manual;
        }
    }

    context.session.workflowPhase = WorkflowPhase::ManualRefinement;
    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, context.session.registrations);
    m_convergenceAnalyzer.Analyze(context.session.registrations, context.session.projectPreferences.preferManualPriors);

    m_lastMessage = "Landmark transform propagated to " + std::to_string(static_cast<int>(affectedPairs.size())) + " pairs.";
}

void MainWindow::PropagateManualLandmarksToInterval(AppContext& context, int fromPairIdx, int toPairIdx)
{
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish.";
        return;
    }

    RegistrationResult* current = GetOrCreateCurrentRegistration(context);
    if (current == nullptr)
    {
        m_lastMessage = "No valid pair selected for landmark propagation.";
        return;
    }

    if (current->landmarks.size() < 2)
    {
        m_lastMessage = "At least 2 landmark pairs are required to propagate to an interval.";
        return;
    }

    RegistrationResult source = *current;
    const Result computeResult = m_landmarkRegistration.ComputeFromLandmarks(source);
    if (!computeResult.ok)
    {
        m_lastMessage = computeResult.message;
        return;
    }

    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, context.session.registrations);

    const int pairCount = static_cast<int>(context.session.pairing.pairs.size());
    fromPairIdx = std::clamp(fromPairIdx, 0, pairCount - 1);
    toPairIdx   = std::clamp(toPairIdx,   fromPairIdx, pairCount - 1);

    std::vector<OperationPairRef> affectedPairs;
    for (int i = fromPairIdx; i <= toPairIdx; ++i)
    {
        const PairRecord& pair = context.session.pairing.pairs[i];
        if (pair.valid)
        {
            affectedPairs.push_back({pair.fixedIndex, pair.movingIndex});
        }
    }

    const int operationId = AppendOperation(
        context.session,
        OperationKind::ManualLandmarks,
        OperationScope::Selection,
        "Propagate manual landmarks to interval [" + std::to_string(fromPairIdx + 1) +
            "-" + std::to_string(toPairIdx + 1) + "]",
        "manual_landmarks",
        affectedPairs,
        source.score);

    for (int i = fromPairIdx; i <= toPairIdx; ++i)
    {
        PairRecord& pair = context.session.pairing.pairs[i];
        if (!pair.valid)
            continue;

        RegistrationResult target = source;
        target.fixedIndex  = pair.fixedIndex;
        target.movingIndex = pair.movingIndex;
        if (pair.fixedIndex != source.fixedIndex || pair.movingIndex != source.movingIndex)
        {
            target.landmarks.clear();
        }
        StoreRegistrationResult(context, target, operationId);
        pair.status = PairStatus::Manual;
    }

    context.session.workflowPhase = WorkflowPhase::ManualRefinement;
    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, context.session.registrations);
    m_convergenceAnalyzer.Analyze(context.session.registrations, context.session.projectPreferences.preferManualPriors);

    m_lastMessage = "Landmark transform propagated to " + std::to_string(static_cast<int>(affectedPairs.size())) +
                    " pairs (interval " + std::to_string(fromPairIdx + 1) + "-" + std::to_string(toPairIdx + 1) + ").";
}

void MainWindow::AnalyzeConvergence(AppContext& context)
{
    if (context.session.registrations.empty())
    {
        m_lastMessage = "Run some registrations before analyzing convergence.";
        return;
    }
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before starting convergence analysis.";
        return;
    }

    std::vector<RegistrationResult> registrations = context.session.registrations;
    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, registrations);
    const bool preferManualPriors = context.session.projectPreferences.preferManualPriors;
    m_backgroundStatus = "Analyzing convergence across the stack...";
    m_convergenceTask = std::async(std::launch::async,
                                   [this, registrations, preferManualPriors]()
                                   {
                                       ConvergenceTaskResult taskResult;
                                       taskResult.registrations = registrations;
                                       m_convergenceAnalyzer.Analyze(taskResult.registrations, preferManualPriors);
                                       for (const RegistrationResult& registration : taskResult.registrations)
                                       {
                                           if (registration.convergenceOutlier)
                                           {
                                               ++taskResult.outlierCount;
                                           }
                                       }
                                       return taskResult;
                                   });
    context.session.workflowPhase = WorkflowPhase::ConvergenceAnalysis;
    m_lastMessage = "Convergence analysis started.";
}

void MainWindow::RunPriorRefinement(AppContext& context)
{
    if (context.session.registrations.empty())
    {
        m_lastMessage = "Run the first pass before prior refinement.";
        return;
    }
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before starting prior refinement.";
        return;
    }

    const bool useAffine = context.session.projectPreferences.transformType == "affine";
    const bool useSigmaBounds = context.session.projectPreferences.useSigmaRestrictedPriorRefinement;
    const double sigmaMultiplier = context.session.projectPreferences.sigmaMultiplier;
    const bool preferManualPriors = context.session.projectPreferences.preferManualPriors;

    // Pre-analyze on main thread so priors are current before the async copy.
    EnsureRegistrationsForValidPairs(context.session.pairing.pairs, context.session.registrations);
    m_convergenceAnalyzer.Analyze(context.session.registrations, preferManualPriors);

    std::vector<RegistrationResult> registrations = context.session.registrations;
    const std::vector<SliceRecord>  fixedSlices   = context.session.stackA.slices;
    const std::vector<SliceRecord>  movingSlices  = context.session.stackB.slices;

    int total = 0;
    for (const RegistrationResult& r : registrations)
    {
        if (r.hasConvergencePrior && !r.isManual)
        {
            ++total;
        }
    }

    auto progress = std::make_shared<GenericTaskProgress>();
    progress->total.store(total);
    m_priorRefinementProgress = progress;

    context.session.workflowPhase = WorkflowPhase::PriorRefinement;
    m_backgroundStatus = "Prior refinement running...";

    m_priorRefinementTask = std::async(
        std::launch::async,
        [registrations, fixedSlices, movingSlices, useAffine, useSigmaBounds, sigmaMultiplier, preferManualPriors, progress]() mutable
        {
            PriorRefinementTaskResult taskResult;
            taskResult.registrations = registrations;

            ImageLoader      imageLoader;
            RegistrationEngine registrationEngine;
            ConvergenceAnalyzer convergenceAnalyzer;

            for (RegistrationResult& reg : taskResult.registrations)
            {
                if (progress->cancelRequested.load())
                {
                    taskResult.cancelled = true;
                    break;
                }

                if (!reg.hasConvergencePrior || reg.isManual)
                {
                    continue;
                }

                if (reg.fixedIndex < 0  || reg.fixedIndex  >= static_cast<int>(fixedSlices.size()) ||
                    reg.movingIndex < 0 || reg.movingIndex >= static_cast<int>(movingSlices.size()))
                {
                    continue;
                }

                {
                    std::scoped_lock lock(progress->statusMutex);
                    progress->statusMessage =
                        "Refining B:" + std::to_string(reg.movingIndex) +
                        " -> A:" + std::to_string(reg.fixedIndex);
                }

                cv::Mat movingImage, fixedImage;
                if (!imageLoader.LoadColorImage(movingSlices[reg.movingIndex].filePath, movingImage).ok ||
                    !imageLoader.LoadColorImage(fixedSlices[reg.fixedIndex].filePath,   fixedImage).ok)
                {
                    progress->attempted.fetch_add(1);
                    continue;
                }

                RegistrationResult refined = reg;
                Result refine = registrationEngine.RefineCtToPhotoFromPrior(
                    movingImage, fixedImage,
                    reg.priorTx, reg.priorTy, reg.priorTheta, reg.priorScale,
                    refined, useAffine,
                    reg.priorSx, reg.priorSy,
                    useSigmaBounds,
                    sigmaMultiplier,
                    reg.priorTxStdDev, reg.priorTyStdDev, reg.priorThetaStdDev, reg.priorScaleStdDev,
                    reg.priorSxStdDev, reg.priorSyStdDev);

                progress->attempted.fetch_add(1);

                if (!refine.ok)
                {
                    continue;
                }

                refined.fixedIndex        = reg.fixedIndex;
                refined.movingIndex       = reg.movingIndex;
                refined.isManual          = false;
                refined.convergenceOutlier = false;
                reg = refined;
                ++taskResult.refined;
            }

            if (!taskResult.cancelled)
            {
                convergenceAnalyzer.Analyze(taskResult.registrations, preferManualPriors);
            }

            return taskResult;
        });

    m_lastMessage = "Prior refinement started.";
}

void MainWindow::CancelPriorRefinement()
{
    if (m_priorRefinementProgress != nullptr)
    {
        m_priorRefinementProgress->cancelRequested.store(true);
        std::scoped_lock lock(m_priorRefinementProgress->statusMutex);
        m_priorRefinementProgress->statusMessage = "Cancellation requested...";
    }
}

MainWindow::BatchSummary MainWindow::BuildBatchSummary(const AppContext& context) const
{
    BatchSummary summary;

    for (const PairRecord& pair : context.session.pairing.pairs)
    {
        if (!pair.valid)
        {
            continue;
        }

        ++summary.attempted;
        if (pair.status == PairStatus::Aligned)
        {
            ++summary.succeeded;
        }
        if (pair.status == PairStatus::Suspect)
        {
            ++summary.failed;
        }
    }

    for (const RegistrationResult& registration : context.session.registrations)
    {
        if (!summary.hasScores)
        {
            summary.bestScore = registration.score;
            summary.worstScore = registration.score;
            summary.hasScores = true;
        }
        else
        {
            summary.bestScore = (std::max)(summary.bestScore, registration.score);
            summary.worstScore = (std::min)(summary.worstScore, registration.score);
        }
        summary.averageScore += registration.score;
    }

    if (summary.hasScores && !context.session.registrations.empty())
    {
        summary.averageScore /= static_cast<double>(context.session.registrations.size());
    }

    return summary;
}

void MainWindow::ProcessAsyncTasks(AppContext& context)
{
    using namespace std::chrono_literals;

    if (m_currentAlignmentTask.has_value() &&
        m_currentAlignmentTask->wait_for(0ms) == std::future_status::ready)
    {
        CurrentAlignmentTaskResult result = m_currentAlignmentTask->get();
        m_currentAlignmentTask.reset();
        m_backgroundStatus.clear();

        if (!result.result.ok)
        {
            m_lastMessage = result.result.message;
        }
        else
        {
            OperationKind operationKind = OperationKind::CurrentAutoAlignment;
            if (context.session.projectPreferences.autoAlignmentMethod == "prior_refinement_only")
            {
                operationKind = OperationKind::PriorRefinement;
            }
            else if (result.workflowPhase == WorkflowPhase::ManualRefinement)
            {
                operationKind = OperationKind::ManualLandmarks;
            }
            const int operationId = AppendOperation(
                context.session,
                operationKind,
                OperationScope::SinglePair,
                "Current pair alignment",
                context.session.projectPreferences.autoAlignmentMethod,
                {OperationPairRef{result.registration.fixedIndex, result.registration.movingIndex}},
                result.registration.score);
            StoreRegistrationResult(context, result.registration, operationId);
            if (result.pairListIndex >= 0 &&
                result.pairListIndex < static_cast<int>(context.session.pairing.pairs.size()))
            {
                context.session.pairing.pairs[result.pairListIndex].status = result.pairStatus;
            }
            if (context.session.workflowPhase == WorkflowPhase::Setup)
            {
                context.session.workflowPhase = result.workflowPhase;
            }
            else if (result.workflowPhase == WorkflowPhase::ManualRefinement)
            {
                context.session.workflowPhase = result.workflowPhase;
            }

            context.selectedHistoryIndex = -1;
            m_lastMessage = "Alignment computed for pair B:" + std::to_string(result.registration.movingIndex) +
                            " -> A:" + std::to_string(result.registration.fixedIndex) +
                            " with score " + std::to_string(result.registration.score);
        }
    }

    if (m_stackLoadTask.has_value() &&
        m_stackLoadTask->wait_for(0ms) == std::future_status::ready)
    {
        StackLoadTaskResult result = m_stackLoadTask->get();
        m_stackLoadTask.reset();
        m_backgroundStatus.clear();

        if (!result.result.ok)
        {
            m_lastMessage = result.result.message;
        }
        else
        {
            if (result.stackId == context.session.stackA.id)
            {
                context.session.stackA = std::move(result.stack);
            }
            else if (result.stackId == context.session.stackB.id)
            {
                context.session.stackB = std::move(result.stack);
            }

            RebuildPairs(context.session);
            context.session.registrations.clear();
            const StackModel& loadedStack =
                result.stackId == context.session.stackA.id ? context.session.stackA : context.session.stackB;
            m_lastMessage = "Loaded " + std::to_string(loadedStack.slices.size()) + " slices from " +
                            loadedStack.directory;
        }
    }

    if (m_convergenceTask.has_value() &&
        m_convergenceTask->wait_for(0ms) == std::future_status::ready)
    {
        ConvergenceTaskResult result = m_convergenceTask->get();
        m_convergenceTask.reset();
        m_backgroundStatus.clear();

        if (!result.result.ok)
        {
            m_lastMessage = result.result.message;
        }
        else
        {
            context.session.registrations = std::move(result.registrations);
            AppendOperation(
                context.session,
                OperationKind::ConvergenceAnalysis,
                OperationScope::Global,
                "Convergence analysis",
                context.session.projectPreferences.autoAlignmentMethod,
                {},
                0.0);
            m_lastMessage = "Convergence analysis complete. Outliers flagged: " +
                            std::to_string(result.outlierCount) + ".";
        }
    }

    if (m_priorRefinementTask.has_value() &&
        m_priorRefinementTask->wait_for(0ms) == std::future_status::ready)
    {
        PriorRefinementTaskResult result = m_priorRefinementTask->get();
        m_priorRefinementTask.reset();
        m_priorRefinementProgress.reset();
        m_backgroundStatus.clear();

        if (!result.result.ok)
        {
            m_lastMessage = result.result.message;
        }
        else if (result.cancelled)
        {
            m_lastMessage = "Prior refinement cancelled. Refined so far: " +
                            std::to_string(result.refined) + ".";
        }
        else
        {
            std::vector<OperationPairRef> operationPairs;
            double totalScore = 0.0;
            int scoreCount = 0;
            for (RegistrationResult& registration : result.registrations)
            {
                if (!registration.refinedWithPrior || registration.isManual)
                {
                    continue;
                }
                operationPairs.push_back({registration.fixedIndex, registration.movingIndex});
                totalScore += registration.score;
                ++scoreCount;
            }
            const int operationId = AppendOperation(
                context.session,
                OperationKind::PriorRefinement,
                operationPairs.size() == 1 ? OperationScope::SinglePair : OperationScope::Global,
                "Prior refinement",
                context.session.projectPreferences.useSigmaRestrictedPriorRefinement ? "prior_sigma_restricted" : "prior_refinement",
                operationPairs,
                scoreCount > 0 ? totalScore / static_cast<double>(scoreCount) : 0.0);
            for (RegistrationResult& registration : result.registrations)
            {
                if (registration.refinedWithPrior && !registration.isManual)
                {
                    AppendHistorySnapshot(registration, BuildSnapshotLabel(registration));
                    if (!registration.history.empty())
                    {
                        registration.history.back().operationId = operationId;
                    }
                }
            }
            context.session.registrations = std::move(result.registrations);
            m_lastMessage = "Prior refinement complete. Refined registrations: " +
                            std::to_string(result.refined) + ".";
        }
    }

    if (m_exportBatchTask.has_value() &&
        m_exportBatchTask->wait_for(0ms) == std::future_status::ready)
    {
        ExportBatchTaskResult result = m_exportBatchTask->get();
        m_exportBatchTask.reset();
        m_exportBatchProgress.reset();
        m_backgroundStatus.clear();

        if (!result.result.ok)
        {
            m_lastMessage = result.result.message;
        }
        else if (result.cancelled)
        {
            m_lastMessage = "Batch export cancelled. Exported so far: " +
                            std::to_string(result.exported) + ".";
        }
        else
        {
            m_lastMessage = "Batch export finished. Exported pairs: " +
                            std::to_string(result.exported) + ".";
        }
    }

    if (m_animatedExportTask.has_value() &&
        m_animatedExportTask->wait_for(0ms) == std::future_status::ready)
    {
        AnimatedExportTaskResult result = m_animatedExportTask->get();
        m_animatedExportTask.reset();
        m_animatedExportProgress.reset();
        m_backgroundStatus.clear();

        if (!result.result.ok)
        {
            m_lastMessage = "Animated export failed: " + result.result.message;
        }
        else if (result.cancelled)
        {
            m_lastMessage = "Animated export cancelled. Frames written: " +
                            std::to_string(result.framesWritten) + ".";
        }
        else
        {
            m_lastMessage = "Animated export finished. Frames: " +
                            std::to_string(result.framesWritten) + ".";
        }
    }
}

bool MainWindow::HasBackgroundTask() const
{
    return m_currentAlignmentTask.has_value() ||
           m_stackLoadTask.has_value()        ||
           m_convergenceTask.has_value()      ||
           m_batchTask.has_value()            ||
           m_priorRefinementTask.has_value()  ||
           m_exportBatchTask.has_value()      ||
           m_animatedExportTask.has_value();
}

void MainWindow::RunTransformInterpolation(AppContext& context)
{
    const int count = ApplyTransformInterpolation(context.session.pairing.pairs,
                                                  context.session.registrations);
    if (count == 0)
    {
        m_lastMessage = "Interpolation: no gaps found between anchor registrations (need at least 2 converged or manual results).";
        return;
    }

    std::vector<OperationPairRef> pairs;
    for (const RegistrationResult& reg : context.session.registrations)
    {
        if (reg.isInterpolated)
            pairs.push_back({reg.fixedIndex, reg.movingIndex});
    }
    AppendOperation(context.session,
                    OperationKind::LandmarkInterpolation,
                    OperationScope::Global,
                    "Interpolate between anchors",
                    "linear_similarity_interpolation",
                    pairs);

    m_lastMessage = "Interpolation complete. " + std::to_string(count) + " slices filled.";
}

void MainWindow::ExportAnimatedPreview(AppContext& context)
{
    if (HasBackgroundTask())
    {
        m_lastMessage = "Wait for the current background task to finish before starting animated export.";
        return;
    }

    const auto savePath = ShowSaveFileDialog(L"Save Animated Preview", L"avi",
                                             L"AVI Video", L"*.avi",
                                             L"animated_preview.avi");
    if (!savePath.has_value())
        return;

    const AnimatedExportSettings anim  = context.session.animatedExport;
    std::vector<PairRecord>        pairs         = context.session.pairing.pairs;
    std::vector<RegistrationResult> registrations = context.session.registrations;
    const std::vector<SliceRecord>  fixedSlices   = context.session.stackA.slices;
    const std::vector<SliceRecord>  movingSlices  = context.session.stackB.slices;
    const std::filesystem::path     outputPath    = *savePath;
    const UiPreferences             uiPrefs       = context.session.uiPreferences;

    const int startIdx = anim.startPairIndex;
    const int endIdx   = anim.endPairIndex < 0 ? static_cast<int>(pairs.size()) - 1 : anim.endPairIndex;
    const int total    = (std::max)(0, endIdx - startIdx + 1);

    auto progress = std::make_shared<GenericTaskProgress>();
    progress->total.store(total);
    m_animatedExportProgress = progress;
    m_backgroundStatus = "Animated export running...";

    m_animatedExportTask = std::async(
        std::launch::async,
        [pairs, registrations, fixedSlices, movingSlices, outputPath,
         anim, uiPrefs, startIdx, endIdx, total, progress]() mutable
        {
            using namespace cv;
            AnimatedExportTaskResult taskResult;
            taskResult.total = total;

            // Determine output resolution from the first valid pair's fixed image.
            Size frameSize(1024, 768);
            for (int i = startIdx; i <= endIdx; ++i)
            {
                if (i < 0 || i >= static_cast<int>(pairs.size())) continue;
                const PairRecord& pair = pairs[i];
                if (!pair.valid) continue;
                if (pair.fixedIndex < 0 || pair.fixedIndex >= static_cast<int>(fixedSlices.size())) continue;
                Mat probe = imread(fixedSlices[pair.fixedIndex].filePath, IMREAD_COLOR);
                if (!probe.empty())
                {
                    frameSize = probe.size();
                    break;
                }
            }

            const int fourcc = VideoWriter::fourcc('M', 'J', 'P', 'G');
            VideoWriter writer(outputPath.string(), fourcc, static_cast<double>(anim.fps), frameSize, true);
            if (!writer.isOpened())
            {
                taskResult.result = {false, "Could not open VideoWriter for: " + outputPath.string()};
                return taskResult;
            }

            ImageLoader loader;
            UiPreferences renderPrefs = uiPrefs;
            if (!anim.useCurrentPreviewMode)
            {
                renderPrefs.previewMode = PreviewMode::Blend;
                renderPrefs.blendAlpha  = anim.blendAlpha;
            }

            for (int i = startIdx; i <= endIdx; ++i)
            {
                if (progress->cancelRequested.load())
                {
                    taskResult.cancelled = true;
                    break;
                }

                {
                    std::scoped_lock lock(progress->statusMutex);
                    progress->statusMessage = "Pair " + std::to_string(i);
                }

                Mat frame;

                if (i >= 0 && i < static_cast<int>(pairs.size()))
                {
                    const PairRecord& pair = pairs[i];
                    if (pair.valid &&
                        pair.fixedIndex  >= 0 && pair.fixedIndex  < static_cast<int>(fixedSlices.size()) &&
                        pair.movingIndex >= 0 && pair.movingIndex < static_cast<int>(movingSlices.size()))
                    {
                        Mat imageA, imageB;
                        loader.LoadColorImage(fixedSlices[pair.fixedIndex].filePath,   imageA);
                        loader.LoadColorImage(movingSlices[pair.movingIndex].filePath, imageB);

                        if (!imageA.empty() && !imageB.empty())
                        {
                            const RegistrationResult* reg =
                                FindRegistrationResult(registrations, pair.fixedIndex, pair.movingIndex);
                            const bool hasTransform = reg != nullptr &&
                                (reg->converged || reg->isManual || !reg->iterations.empty());

                            Mat movingWarped = imageB;
                            if (hasTransform)
                            {
                                Mat affine = (Mat_<double>(2, 3) <<
                                    reg->forward.matrix[0], reg->forward.matrix[1], reg->forward.matrix[2],
                                    reg->forward.matrix[3], reg->forward.matrix[4], reg->forward.matrix[5]);
                                warpAffine(imageB, movingWarped, affine, imageA.size(),
                                           INTER_LINEAR, BORDER_CONSTANT, Scalar(0, 0, 0));
                            }

                            // BuildPreviewImage logic inlined (avoids sharing the free function across TUs)
                            Mat resizedB;
                            resize(movingWarped, resizedB, imageA.size(), 0.0, 0.0, INTER_LINEAR);

                            switch (renderPrefs.previewMode)
                            {
                            case PreviewMode::Blend:
                                addWeighted(imageA, 1.0 - renderPrefs.blendAlpha,
                                            resizedB, renderPrefs.blendAlpha, 0.0, frame);
                                break;
                            case PreviewMode::Checkerboard:
                            {
                                frame = imageA.clone();
                                const int tile = (std::max)(4, renderPrefs.checkerSize);
                                for (int y = 0; y < frame.rows; y += tile)
                                {
                                    for (int x = 0; x < frame.cols; x += tile)
                                    {
                                        if (((x / tile) + (y / tile)) % 2 == 0) continue;
                                        const int w = (std::min)(tile, frame.cols - x);
                                        const int h = (std::min)(tile, frame.rows - y);
                                        resizedB(Rect(x, y, w, h)).copyTo(frame(Rect(x, y, w, h)));
                                    }
                                }
                                break;
                            }
                            case PreviewMode::Difference:
                                absdiff(imageA, resizedB, frame);
                                break;
                            case PreviewMode::Multiply:
                            {
                                Mat fa, fb;
                                imageA.convertTo(fa,  CV_32FC3, 1.0 / 255.0);
                                resizedB.convertTo(fb, CV_32FC3, 1.0 / 255.0);
                                multiply(fa, fb, frame);
                                frame.convertTo(frame, CV_8UC3, 255.0);
                                break;
                            }
                            default:
                                frame = imageA.clone();
                                break;
                            }
                        }
                    }
                }

                if (frame.empty())
                {
                    frame = Mat::zeros(frameSize, CV_8UC3);
                }
                else if (frame.size() != frameSize)
                {
                    resize(frame, frame, frameSize, 0.0, 0.0, INTER_LINEAR);
                }

                writer.write(frame);
                ++taskResult.framesWritten;
                progress->attempted.fetch_add(1);
            }

            writer.release();
            return taskResult;
        });

    m_lastMessage = "Animated export started.";
}

void MainWindow::CancelAnimatedExport()
{
    if (m_animatedExportProgress != nullptr)
    {
        m_animatedExportProgress->cancelRequested.store(true);
        std::scoped_lock lock(m_animatedExportProgress->statusMutex);
        m_animatedExportProgress->statusMessage = "Cancellation requested...";
    }
}

// ──────────────────────────────────────────────────────────────
// Operation history panel
// ──────────────────────────────────────────────────────────────

void MainWindow::DrawOperationStack(AppContext& context)
{
    ImGui::TextUnformatted("Operation Stack");
    ShowHoveredHelp("Chronological stack of executed operations. Select one to preview the corresponding transform for the current pair when available.");

    if (context.session.operations.empty())
    {
        ImGui::TextDisabled("No operations recorded yet.");
        return;
    }

    if (ImGui::Button("Show Latest State"))
    {
        context.selectedOperationId = 0;
        context.selectedHistoryIndex = -1;
    }

    for (int i = static_cast<int>(context.session.operations.size()) - 1; i >= 0; --i)
    {
        const AlignmentOperation& operation = context.session.operations[static_cast<size_t>(i)];
        std::ostringstream label;
        label << OperationKindLabel(operation.kind) << " | " << operation.affectedPairs;
        if (!operation.label.empty())
        {
            label << " | " << operation.label;
        }

        const bool selected = context.selectedOperationId == operation.id;
        if (ImGui::Selectable(label.str().c_str(), selected))
        {
            context.selectedOperationId = operation.id;
            context.selectedHistoryIndex = -1;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Scope: %s", operation.scope == OperationScope::Global ? "Global"
                                                                              : (operation.scope == OperationScope::Selection ? "Selection"
                                                                                                                              : "Single Pair"));
            ImGui::Text("Method: %s", operation.method.empty() ? "n/a" : operation.method.c_str());
            ImGui::Text("Affected pairs: %d", operation.affectedPairs);
            if (!operation.timestamp.empty())
            {
                ImGui::TextUnformatted(operation.timestamp.c_str());
            }
            ImGui::EndTooltip();
        }
    }
}

void MainWindow::DrawOperationHistory(AppContext& context)
{
    ImGui::TextUnformatted("Operation History");
    ShowHoveredHelp("Select a previous alignment state to preview it in the composite viewer. Delete entries to roll back audit history for this pair.");

    RegistrationResult* reg = GetOrCreateCurrentRegistration(context);

    if (reg == nullptr)
    {
        ImGui::TextDisabled("No registration for current pair.");
        return;
    }

    if (reg->history.empty())
    {
        if (!reg->operationLog.empty())
        {
            for (const std::string& entry : reg->operationLog)
            {
                ImGui::TextWrapped("%s", entry.c_str());
            }
        }
        else
        {
            ImGui::TextDisabled("No stored history yet.");
        }
        return;
    }

    const int historyCount = static_cast<int>(reg->history.size());
    context.selectedHistoryIndex = (std::clamp)(context.selectedHistoryIndex, -1, historyCount - 1);

    if (ImGui::Button("Show Latest"))
    {
        context.selectedHistoryIndex = -1;
    }
    ImGui::SameLine();
    const bool canDelete = context.selectedHistoryIndex >= 0 &&
                           context.selectedHistoryIndex < historyCount;
    if (!canDelete)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Selected"))
    {
        const int deleteIndex = context.selectedHistoryIndex;
        reg->history.erase(reg->history.begin() + context.selectedHistoryIndex);
        context.selectedHistoryIndex = -1;
        if (!reg->history.empty())
        {
            const RegistrationSnapshot& latest = reg->history.back();
            reg->forward = latest.forward;
            reg->inverse = latest.inverse;
            reg->transformType = latest.transformType;
            reg->score = latest.score;
            reg->manualRmsError = latest.manualRmsError;
            reg->isManual = latest.isManual;
        }
        else if (deleteIndex >= 0)
        {
            reg->forward = Transform2D{};
            reg->inverse = Transform2D{};
            reg->score = 0.0;
            reg->manualRmsError = 0.0;
            reg->converged = false;
            reg->isManual = false;
            reg->transformType = "none";
            reg->iterations.clear();
        }
        return;
    }
    if (!canDelete)
    {
        ImGui::EndDisabled();
    }

    for (int i = historyCount - 1; i >= 0; --i)
    {
        const RegistrationSnapshot& snapshot = reg->history[static_cast<size_t>(i)];
        const bool selected = context.selectedHistoryIndex == i;
        std::string label = snapshot.label.empty() ? ("Step " + std::to_string(i + 1)) : snapshot.label;
        if (ImGui::Selectable(label.c_str(), selected))
        {
            context.selectedHistoryIndex = i;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Score: %.4f", snapshot.score);
            ImGui::Text("Transform: %s", snapshot.transformType.c_str());
            if (snapshot.isManual)
            {
                ImGui::Text("Manual RMS: %.3f px", snapshot.manualRmsError);
            }
            if (!snapshot.timestamp.empty())
            {
                ImGui::TextUnformatted(snapshot.timestamp.c_str());
            }
            ImGui::EndTooltip();
        }
    }

    if (reg->convergenceOutlier)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "[!] Convergence outlier");

    if (!reg->timestamp.empty())
        ImGui::TextDisabled("Last updated: %s", reg->timestamp.c_str());
}

// ──────────────────────────────────────────────────────────────
// Per-slice transform metrics graph
// ──────────────────────────────────────────────────────────────

void MainWindow::DrawMetricsGraph(AppContext& context)
{
    TextWithHelp("Stack Metrics", "Plots one transform parameter across the stack so you can spot drift, jumps, or outliers between neighboring slices.");

    const auto& regs = context.session.registrations;
    if (regs.empty())
    {
        ImGui::TextDisabled("No registrations available.");
        return;
    }

    // Sort by moving index for a left-to-right stack view
    std::vector<const RegistrationResult*> sorted;
    sorted.reserve(regs.size());
    for (const auto& r : regs)
        sorted.push_back(&r);
    std::sort(sorted.begin(), sorted.end(),
              [](const RegistrationResult* a, const RegistrationResult* b) {
                  return a->movingIndex < b->movingIndex;
              });

    const int n = static_cast<int>(sorted.size());

    // Build per-parameter float arrays
    std::vector<float> txVals(n), tyVals(n), thetaVals(n), scaleVals(n), scoreVals(n);
    std::vector<bool>  outlierFlags(n, false);

    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < n; ++i)
    {
        const RegistrationResult& r = *sorted[i];
        if (!r.iterations.empty())
        {
            const IterationRecord& it = r.iterations.back();
            txVals[i]    = static_cast<float>(it.tx);
            tyVals[i]    = static_cast<float>(it.ty);
            thetaVals[i] = static_cast<float>(it.theta * 180.0 / kPi);
            // For affine show sx; for similarity show scale
            scaleVals[i] = static_cast<float>(it.sx > 0.0 ? it.sx : it.scale);
        }
        scoreVals[i]   = static_cast<float>(r.score);
        outlierFlags[i] = r.convergenceOutlier;
    }

    // Parameter selector
    static int s_paramIdx = 0;
    static const char* kParamLabels[] = {"tx (px)", "ty (px)", "theta (deg)", "scale / sx", "score"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##ParamSel", &s_paramIdx, kParamLabels, IM_ARRAYSIZE(kParamLabels));
    ShowHoveredHelp("Choose which registration parameter to plot along the stack.");

    const std::vector<float>* data = nullptr;
    switch (s_paramIdx)
    {
    case 0:  data = &txVals;    break;
    case 1:  data = &tyVals;    break;
    case 2:  data = &thetaVals; break;
    case 3:  data = &scaleVals; break;
    default: data = &scoreVals; break;
    }

    // Stats
    const float sum  = std::accumulate(data->begin(), data->end(), 0.0f);
    const float mean = n > 0 ? sum / static_cast<float>(n) : 0.0f;
    float varAcc = 0.0f;
    for (float v : *data) varAcc += (v - mean) * (v - mean);
    const float stdDev = n > 1 ? std::sqrt(varAcc / static_cast<float>(n)) : 0.0f;

    const float vMin = *std::min_element(data->begin(), data->end());
    const float vMax = *std::max_element(data->begin(), data->end());
    const float pad  = (vMax - vMin) * 0.12f + 0.5f;

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "mean=%.2f  std=%.2f", mean, stdDev);

    ImGui::PlotLines("##Metrics", data->data(), n, 0, overlay,
                     vMin - pad, vMax + pad, ImVec2(-1.0f, 70.0f));

    // Outlier summary
    int outlierCount = 0;
    for (bool o : outlierFlags) if (o) ++outlierCount;
    if (outlierCount > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "Outliers (%d):", outlierCount);
        for (int i = 0; i < n; ++i)
        {
            if (outlierFlags[i])
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "B%d", sorted[i]->movingIndex);
            }
        }
    }
}

} // namespace align
