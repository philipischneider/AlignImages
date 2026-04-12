#include "ui/MainWindow.h"

#include "core/WinDialogUtils.h"
#include "core/DpiUtilsWin.h"

#include "imgui.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
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
    InputTextString("Transform", context.session.projectPreferences.transformType);
    ImGui::TextWrapped("%s", DescribeCurrentAutomaticPipeline(context.session.projectPreferences));
    ImGui::SliderInt("Max Iterations", &context.session.projectPreferences.maxIterations, 1, 250);
    ImGui::SliderInt("Coarse Levels", &context.session.projectPreferences.coarseLevels, 1, 6);
    ImGui::Checkbox("Use Alignment In Preview", &context.session.projectPreferences.useAlignmentPreview);
    if (ImGui::Button("Run Current Alignment"))
    {
        RunCurrentAlignment(context);
    }
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
    if (ImGui::Button("Apply Manual Landmarks"))
    {
        ApplyManualLandmarks(context);
    }
    if (ImGui::Button("Analyze Convergence"))
    {
        AnalyzeConvergence(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Run Prior Refinement"))
    {
        RunPriorRefinement(context);
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
    ImGui::TextUnformatted("Diagnostics");
    ImGui::Text("Stack A slices: %d", static_cast<int>(context.session.stackA.slices.size()));
    ImGui::Text("Stack B slices: %d", static_cast<int>(context.session.stackB.slices.size()));
    ImGui::Text("Global Offset: %d", context.session.pairing.globalOffset);
    ImGui::Text("Preview: %s", PreviewModeLabel(context.session.uiPreferences.previewMode));
    ImGui::Text("Auto Method: %s", context.session.projectPreferences.autoAlignmentMethod.c_str());
    ImGui::Text("Mask Strategy: %s", context.session.projectPreferences.maskMethod.c_str());
    ImGui::Text("Score Strategy: %s", context.session.projectPreferences.scoreMethod.c_str());
    ImGui::Text("Refinement: %s", context.session.projectPreferences.refinementMethod.c_str());
    ImGui::Text("Pairs tracked: %d", static_cast<int>(context.session.pairing.pairs.size()));
    const BatchSummary summary = BuildBatchSummary(context);
    ImGui::Text("Registrations: %d", static_cast<int>(context.session.registrations.size()));
    ImGui::Separator();

    ImGui::TextUnformatted("Batch Summary");
    ImGui::Text("Attempted: %d", summary.attempted);
    ImGui::Text("Succeeded: %d", summary.succeeded);
    ImGui::Text("Failed: %d", summary.failed);
    ImGui::Text("Batch Running: %s", context.batchProcessState.running ? "yes" : "no");
    if (summary.hasScores)
    {
        ImGui::Text("Average Score: %.4f", summary.averageScore);
        ImGui::Text("Best Score: %.4f", summary.bestScore);
        ImGui::Text("Worst Score: %.4f", summary.worstScore);
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Current Selection");
    ImGui::Text("Reference Slice A: %d", context.session.projectPreferences.activeSliceA);
    ImGui::Text("Moving Slice B: %d", context.session.projectPreferences.activeSliceB);
    ImGui::Separator();

    ImGui::TextUnformatted("Registration Status");
    const RegistrationResult* registration = FindRegistrationResult(
        context.session.registrations,
        context.session.projectPreferences.activeSliceA,
        context.session.projectPreferences.activeSliceB);
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
        if (registration->isManual)
        {
            ImGui::Text("RMS Error: %.4f px", registration->manualRmsError);
        }
        if (!registration->iterations.empty())
        {
            const IterationRecord& iteration = registration->iterations.back();
            ImGui::Text("tx: %.2f", iteration.tx);
            ImGui::Text("ty: %.2f", iteration.ty);
            ImGui::Text("theta: %.2f deg", iteration.theta * 180.0 / 3.14159265358979323846);
            ImGui::Text("scale: %.4f", iteration.scale);
        }
    }
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

void MainWindow::LoadStack(AppContext& context, StackModel& stack)
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

    cv::Mat movingImage;
    cv::Mat fixedImage;
    Result loadMoving = m_imageLoader.LoadColorImage(movingSlice.filePath, movingImage);
    Result loadFixed = m_imageLoader.LoadColorImage(fixedSlice.filePath, fixedImage);
    if (!loadMoving.ok || !loadFixed.ok)
    {
        m_lastMessage = "Could not load the active pair for registration.";
        return;
    }

    RegistrationResult computed;
    computed.fixedIndex = pair.fixedIndex;
    computed.movingIndex = pair.movingIndex;
    Result registration;
    if (context.session.projectPreferences.autoAlignmentMethod == "landmarks_only")
    {
        RegistrationResult* existing =
            FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
        if (existing == nullptr || existing->landmarks.size() < 2)
        {
            m_lastMessage = "Landmarks Only requires at least 2 landmark pairs for the current selection.";
            return;
        }

        registration = m_landmarkRegistration.ComputeFromLandmarks(*existing);
        if (!registration.ok)
        {
            m_lastMessage = registration.message;
            return;
        }

        existing->fixedIndex = pair.fixedIndex;
        existing->movingIndex = pair.movingIndex;
        existing->isManual = true;
        PairRecord& mutablePair = context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
        mutablePair.status = PairStatus::Manual;
        m_lastMessage = "Manual landmark alignment applied to the current pair.";
        return;
    }
    else if (context.session.projectPreferences.autoAlignmentMethod == "prior_refinement_only")
    {
        RegistrationResult* existing =
            FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
        if (existing == nullptr || !existing->hasConvergencePrior)
        {
            m_lastMessage = "Prior Refinement Only requires an existing registration with convergence prior.";
            return;
        }

        computed = *existing;
        registration = m_registrationEngine.RefineCtToPhotoFromPrior(movingImage,
                                                                     fixedImage,
                                                                     existing->priorTx,
                                                                     existing->priorTy,
                                                                     existing->priorTheta,
                                                                     existing->priorScale,
                                                                     computed);
    }
    else
    {
        registration = m_registrationEngine.RegisterCtToPhoto(movingImage, fixedImage, computed);
    }
    if (!registration.ok)
    {
        m_lastMessage = registration.message;
        return;
    }

    StoreRegistrationResult(context, computed);
    PairRecord& mutablePair = context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
    mutablePair.status = computed.score >= 0.15 ? PairStatus::Aligned : PairStatus::Suspect;

    m_lastMessage = "Alignment computed for pair B:" + std::to_string(computed.movingIndex) +
                    " -> A:" + std::to_string(computed.fixedIndex) +
                    " with score " + std::to_string(computed.score);
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
    const std::vector<SliceRecord> fixedSlices = context.session.stackA.slices;
    const std::vector<SliceRecord> movingSlices = context.session.stackB.slices;
    const std::string autoMethod = context.session.projectPreferences.autoAlignmentMethod;
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
    m_batchTask = std::async(std::launch::async,
                             [pairs, registrations, fixedSlices, movingSlices, autoMethod, progress]()
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
                                             movingImage,
                                             fixedImage,
                                             existing->priorTx,
                                             existing->priorTy,
                                             existing->priorTheta,
                                             existing->priorScale,
                                             computed);
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
                                         registration = registrationEngine.RegisterCtToPhoto(movingImage, fixedImage, computed);
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

    m_lastMessage = "Current pair exported to " + outputDir->string();
}

void MainWindow::ExportBatchAligned(AppContext& context)
{
    const auto outputDir = ShowSelectFolderDialog(L"Select Batch Export Folder");
    if (!outputDir.has_value())
    {
        return;
    }

    int exported = 0;

    for (const PairRecord& pair : context.session.pairing.pairs)
    {
        if (!pair.valid)
        {
            continue;
        }

        const RegistrationResult* registration =
            FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
        if (registration == nullptr)
        {
            continue;
        }

        const SliceRecord& fixedSlice = context.session.stackA.slices[pair.fixedIndex];
        const SliceRecord& movingSlice = context.session.stackB.slices[pair.movingIndex];

        const std::filesystem::path baseDir = *outputDir;
        const std::filesystem::path movingToFixedPath =
            baseDir / "moving_to_fixed" /
            ("B" + std::to_string(pair.movingIndex) + "_to_A" + std::to_string(pair.fixedIndex) + ".png");
        const std::filesystem::path fixedToMovingPath =
            baseDir / "fixed_to_moving" /
            ("A" + std::to_string(pair.fixedIndex) + "_to_B" + std::to_string(pair.movingIndex) + ".png");

        Result exportForward = m_exportController.ExportAlignedMovingToFixed(
            movingSlice.filePath, fixedSlice.filePath, *registration, movingToFixedPath);
        Result exportInverse = m_exportController.ExportAlignedFixedToMoving(
            fixedSlice.filePath, movingSlice.filePath, *registration, fixedToMovingPath);
        if (exportForward.ok && exportInverse.ok)
        {
            ++exported;
        }
    }

    m_lastMessage = "Batch export finished. Exported pairs: " + std::to_string(exported);
}

void MainWindow::StoreRegistrationResult(AppContext& context, const RegistrationResult& computed)
{
    RegistrationResult* existing =
        FindRegistrationResult(context.session.registrations, computed.fixedIndex, computed.movingIndex);
    if (existing != nullptr)
    {
        *existing = computed;
    }
    else
    {
        context.session.registrations.push_back(computed);
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
}

void MainWindow::ApplyManualLandmarks(AppContext& context)
{
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

    if (context.session.projectPreferences.activeSliceA >= 0 &&
        context.session.projectPreferences.activeSliceA < static_cast<int>(context.session.pairing.pairs.size()))
    {
        PairRecord& pair = context.session.pairing.pairs[context.session.projectPreferences.activeSliceA];
        pair.status = PairStatus::Manual;
    }

    m_lastMessage = "Manual landmark alignment applied to the current pair.";
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

    const std::vector<RegistrationResult> registrations = context.session.registrations;
    m_backgroundStatus = "Analyzing convergence across the stack...";
    m_convergenceTask = std::async(std::launch::async,
                                   [this, registrations]()
                                   {
                                       ConvergenceTaskResult taskResult;
                                       taskResult.registrations = registrations;
                                       m_convergenceAnalyzer.Analyze(taskResult.registrations);
                                       for (const RegistrationResult& registration : taskResult.registrations)
                                       {
                                           if (registration.convergenceOutlier)
                                           {
                                               ++taskResult.outlierCount;
                                           }
                                       }
                                       return taskResult;
                                   });
    m_lastMessage = "Convergence analysis started.";
}

void MainWindow::RunPriorRefinement(AppContext& context)
{
    if (context.session.registrations.empty())
    {
        m_lastMessage = "Run the first pass before prior refinement.";
        return;
    }

    m_convergenceAnalyzer.Analyze(context.session.registrations);

    int refined = 0;
    for (RegistrationResult& registration : context.session.registrations)
    {
        if (!registration.hasConvergencePrior || registration.isManual)
        {
            continue;
        }

        if (registration.fixedIndex < 0 || registration.fixedIndex >= static_cast<int>(context.session.stackA.slices.size()) ||
            registration.movingIndex < 0 || registration.movingIndex >= static_cast<int>(context.session.stackB.slices.size()))
        {
            continue;
        }

        const SliceRecord& fixedSlice = context.session.stackA.slices[registration.fixedIndex];
        const SliceRecord& movingSlice = context.session.stackB.slices[registration.movingIndex];
        cv::Mat movingImage;
        cv::Mat fixedImage;
        Result loadMoving = m_imageLoader.LoadColorImage(movingSlice.filePath, movingImage);
        Result loadFixed = m_imageLoader.LoadColorImage(fixedSlice.filePath, fixedImage);
        if (!loadMoving.ok || !loadFixed.ok)
        {
            continue;
        }

        RegistrationResult refinedResult = registration;
        Result refine = m_registrationEngine.RefineCtToPhotoFromPrior(
            movingImage,
            fixedImage,
            registration.priorTx,
            registration.priorTy,
            registration.priorTheta,
            registration.priorScale,
            refinedResult);
        if (!refine.ok)
        {
            continue;
        }

        refinedResult.fixedIndex = registration.fixedIndex;
        refinedResult.movingIndex = registration.movingIndex;
        refinedResult.isManual = false;
        refinedResult.convergenceOutlier = false;
        registration = refinedResult;
        ++refined;
    }

    m_convergenceAnalyzer.Analyze(context.session.registrations);
    m_lastMessage = "Prior refinement complete. Refined registrations: " + std::to_string(refined) + ".";
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
            m_lastMessage = "Convergence analysis complete. Outliers flagged: " +
                            std::to_string(result.outlierCount) + ".";
        }
    }
}

bool MainWindow::HasBackgroundTask() const
{
    return m_stackLoadTask.has_value() || m_convergenceTask.has_value() || m_batchTask.has_value();
}
} // namespace align
