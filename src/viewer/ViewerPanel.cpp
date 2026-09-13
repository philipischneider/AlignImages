#include "viewer/ViewerPanel.h"

#include "core/Transform2DMath.h"
#include "io/DicomLoader.h"
#include "registration/LandmarkRegistration.h"

#include "imgui.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace align
{
namespace
{
void DrawWindowLevelControls(const char* idSuffix, const char* label, StackModel& stack)
{
    if (!stack.isDicom)
    {
        return;
    }

    ImGui::PushID(idSuffix);
    if (label != nullptr)
    {
        ImGui::TextUnformatted(label);
    }
    float center = static_cast<float>(stack.windowCenter);
    float width = static_cast<float>(stack.windowWidth);
    bool changed = false;
    changed |= ImGui::SliderFloat("Window Center", &center, -1000.0f, 3000.0f, "%.0f");
    changed |= ImGui::SliderFloat("Window Width", &width, 1.0f, 4000.0f, "%.0f");
    if (changed)
    {
        stack.windowCenter = center;
        stack.windowWidth = (std::max)(width, 1.0f);
    }

    if (stack.modality == "ct")
    {
        if (ImGui::Button("Soft Tissue"))
        {
            stack.windowCenter = 40.0;
            stack.windowWidth = 400.0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Lung"))
        {
            stack.windowCenter = -600.0;
            stack.windowWidth = 1500.0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bone"))
        {
            stack.windowCenter = 400.0;
            stack.windowWidth = 1800.0;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Reset Window"))
    {
        stack.windowCenter = stack.defaultWindowCenter;
        stack.windowWidth = stack.defaultWindowWidth;
    }
    ImGui::PopID();
}

const PairRecord* GetActivePair(const AppContext& context)
{
    const int activeSliceA = GetActivePairing(context.session).activeFixedIndex;
    if (activeSliceA < 0 || activeSliceA >= static_cast<int>(GetActivePairing(context.session).pairs.size()))
    {
        return nullptr;
    }

    const PairRecord& pair = GetActivePairing(context.session).pairs[activeSliceA];
    return pair.valid ? &pair : nullptr;
}

const RegistrationResult* FindCurrentRegistration(const AppContext& context)
{
    const PairRecord* pair = GetActivePair(context);
    if (pair == nullptr)
    {
        return nullptr;
    }

    return FindRegistrationResult(context.session.registrations, GetActivePairing(context.session).fixedStackId, GetActivePairing(context.session).movingStackId, pair->fixedIndex, pair->movingIndex);
}

const RegistrationResult* ResolvePreviewRegistration(const AppContext& context,
                                                     const RegistrationResult* currentRegistration,
                                                     RegistrationResult& historySelection)
{
    if (currentRegistration == nullptr)
    {
        return nullptr;
    }

    if (context.selectedOperationId != 0)
    {
        for (const RegistrationSnapshot& snapshot : currentRegistration->history)
        {
            if (snapshot.operationId != context.selectedOperationId)
            {
                continue;
            }

            historySelection = *currentRegistration;
            historySelection.forward = snapshot.forward;
            historySelection.inverse = snapshot.inverse;
            historySelection.transformType = snapshot.transformType;
            historySelection.score = snapshot.score;
            historySelection.manualRmsError = snapshot.manualRmsError;
            historySelection.isManual = snapshot.isManual;
            return &historySelection;
        }
    }

    const int selectedHistoryIndex = context.selectedHistoryIndex;
    if (selectedHistoryIndex < 0 ||
        selectedHistoryIndex >= static_cast<int>(currentRegistration->history.size()))
    {
        return currentRegistration;
    }

    const RegistrationSnapshot& snapshot = currentRegistration->history[static_cast<size_t>(selectedHistoryIndex)];
    historySelection = *currentRegistration;
    historySelection.forward = snapshot.forward;
    historySelection.inverse = snapshot.inverse;
    historySelection.transformType = snapshot.transformType;
    historySelection.score = snapshot.score;
    historySelection.manualRmsError = snapshot.manualRmsError;
    historySelection.isManual = snapshot.isManual;
    return &historySelection;
}

bool HasUsableTransform(const RegistrationResult* registration)
{
    if (registration == nullptr)
    {
        return false;
    }

    return registration->converged || registration->isManual || !registration->iterations.empty() ||
           registration->hasManualAdjustment;
}

bool HasFixedPoint(const LandmarkPair& landmark)
{
    return std::isfinite(landmark.fixedX) && std::isfinite(landmark.fixedY);
}

bool HasMovingPoint(const LandmarkPair& landmark)
{
    return std::isfinite(landmark.movingX) && std::isfinite(landmark.movingY);
}

// Finds the first landmark still missing a point on the requested side (moving or fixed),
// so a click can complete an already-started pair regardless of which side is clicked first.
int FindOpenLandmarkIndex(const RegistrationResult& registration, bool needMovingPoint)
{
    for (int i = 0; i < static_cast<int>(registration.landmarks.size()); ++i)
    {
        const LandmarkPair& landmark = registration.landmarks[i];
        const bool missing = needMovingPoint ? !HasMovingPoint(landmark) : !HasFixedPoint(landmark);
        if (missing)
        {
            return i;
        }
    }

    return -1;
}

void UpdateManualPreviewRegistration(RegistrationResult& registration)
{
    std::vector<LandmarkPair> completedLandmarks;
    completedLandmarks.reserve(registration.landmarks.size());
    for (const LandmarkPair& landmark : registration.landmarks)
    {
        if (HasFixedPoint(landmark) && HasMovingPoint(landmark))
        {
            completedLandmarks.push_back(landmark);
        }
    }

    if (completedLandmarks.size() < 2)
    {
        registration.transformType = "manual_landmarks_pending";
        registration.forward = Transform2D{};
        registration.inverse = Transform2D{};
        registration.score = 0.0;
        registration.manualRmsError = 0.0;
        registration.converged = false;
        registration.isManual = false;
        registration.iterations.clear();
        UpdateRegistrationBasePreservingAdjustment(registration);
        return;
    }

    RegistrationResult preview = registration;
    preview.landmarks = std::move(completedLandmarks);
    LandmarkRegistration landmarkRegistration;
    if (landmarkRegistration.ComputeFromLandmarks(preview).ok)
    {
        preview.fixedIndex = registration.fixedIndex;
        preview.movingIndex = registration.movingIndex;
        preview.landmarks = registration.landmarks;
        UpdateRegistrationBasePreservingAdjustment(preview);
        registration = std::move(preview);
    }
}

RegistrationResult* GetOrCreateCurrentRegistration(AppContext& context)
{
    const PairRecord* pair = GetActivePair(context);
    if (pair == nullptr)
    {
        return nullptr;
    }

    RegistrationResult* existing =
        FindRegistrationResult(context.session.registrations, GetActivePairing(context.session).fixedStackId, GetActivePairing(context.session).movingStackId, pair->fixedIndex, pair->movingIndex);
    if (existing != nullptr)
    {
        return existing;
    }

    RegistrationResult created;
    created.fixedStackId = GetActivePairing(context.session).fixedStackId;
    created.movingStackId = GetActivePairing(context.session).movingStackId;
    created.fixedIndex = pair->fixedIndex;
    created.movingIndex = pair->movingIndex;
    context.session.registrations.push_back(created);
    return &context.session.registrations.back();
}

void ExtractTransformParameters(const RegistrationResult* registration,
                                double& tx,
                                double& ty,
                                double& theta,
                                double& scale)
{
    tx = 0.0;
    ty = 0.0;
    theta = 0.0;
    scale = 1.0;

    if (registration == nullptr)
    {
        return;
    }

    // A Transpose adjustment mutates forward/inverse directly without touching the optimizer's
    // iteration history, so once one is applied the iteration record is stale -- decompose the
    // live matrix instead, otherwise cache-comparisons elsewhere (see m_loadedTx/Ty/Theta/Scale)
    // would never notice the change and the preview would never refresh.
    if (!registration->hasManualAdjustment && !registration->iterations.empty())
    {
        const IterationRecord& iteration = registration->iterations.back();
        tx = iteration.tx;
        ty = iteration.ty;
        theta = iteration.theta;
        scale = iteration.scale;
        return;
    }

    tx = registration->forward.matrix[2];
    ty = registration->forward.matrix[5];
    scale = std::sqrt(registration->forward.matrix[0] * registration->forward.matrix[0] +
                      registration->forward.matrix[3] * registration->forward.matrix[3]);
    theta = std::atan2(registration->forward.matrix[3], registration->forward.matrix[0]);
}

cv::Mat ApplyTransformToMoving(const cv::Mat& moving, const cv::Size& targetSize, const Transform2D& transform)
{
    const cv::Size resolvedSize = targetSize.width > 0 && targetSize.height > 0 ? targetSize : moving.size();
    cv::Mat affine = (cv::Mat_<double>(2, 3) << transform.matrix[0], transform.matrix[1], transform.matrix[2],
                       transform.matrix[3], transform.matrix[4], transform.matrix[5]);
    cv::Mat warped;
    cv::warpAffine(moving, warped, affine, resolvedSize, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return warped;
}

cv::Mat BuildPreviewImage(const cv::Mat& a, const cv::Mat& b, const UiPreferences& preferences)
{
    if (a.empty() || b.empty())
    {
        return {};
    }

    cv::Mat resizedB;
    cv::resize(b, resizedB, a.size(), 0.0, 0.0, cv::INTER_LINEAR);

    switch (preferences.previewMode)
    {
    case PreviewMode::Blend:
    {
        cv::Mat output;
        cv::addWeighted(a, 1.0 - preferences.blendAlpha, resizedB, preferences.blendAlpha, 0.0, output);
        return output;
    }
    case PreviewMode::Checkerboard:
    {
        cv::Mat output = a.clone();
        const int tileSize = (std::max)(4, preferences.checkerSize);
        for (int y = 0; y < output.rows; y += tileSize)
        {
            for (int x = 0; x < output.cols; x += tileSize)
            {
                const bool useB = ((x / tileSize) + (y / tileSize)) % 2 == 1;
                if (!useB)
                {
                    continue;
                }

                const int width = (std::min)(tileSize, output.cols - x);
                const int height = (std::min)(tileSize, output.rows - y);
                resizedB(cv::Rect(x, y, width, height)).copyTo(output(cv::Rect(x, y, width, height)));
            }
        }
        return output;
    }
    case PreviewMode::Difference:
    {
        cv::Mat output;
        cv::absdiff(a, resizedB, output);
        return output;
    }
    case PreviewMode::Multiply:
    {
        cv::Mat floatA;
        cv::Mat floatB;
        a.convertTo(floatA, CV_32FC3, 1.0 / 255.0);
        resizedB.convertTo(floatB, CV_32FC3, 1.0 / 255.0);
        cv::Mat output;
        cv::multiply(floatA, floatB, output);
        output.convertTo(output, CV_8UC3, 255.0);
        return output;
    }
    default:
        return a.clone();
    }
}

const SliceRecord* GetActiveSlice(const StackModel& stack, int index)
{
    if (index < 0 || index >= static_cast<int>(stack.slices.size()))
    {
        return nullptr;
    }

    return &stack.slices[index];
}

ImVec2 ToScreenPoint(double x, double y, const ImVec2& imageMin, const ImVec2& imageMax, int imageWidth, int imageHeight)
{
    const float u = imageWidth > 0 ? static_cast<float>(x / static_cast<double>(imageWidth)) : 0.0f;
    const float v = imageHeight > 0 ? static_cast<float>(y / static_cast<double>(imageHeight)) : 0.0f;
    return ImVec2(imageMin.x + (imageMax.x - imageMin.x) * u, imageMin.y + (imageMax.y - imageMin.y) * v);
}

bool TryMapMouseToImage(const ImVec2& mousePos,
                        const ImVec2& imageMin,
                        const ImVec2& imageMax,
                        int imageWidth,
                        int imageHeight,
                        double& imageX,
                        double& imageY)
{
    if (mousePos.x < imageMin.x || mousePos.x > imageMax.x || mousePos.y < imageMin.y || mousePos.y > imageMax.y)
    {
        return false;
    }

    const float width = imageMax.x - imageMin.x;
    const float height = imageMax.y - imageMin.y;
    if (width <= 0.0f || height <= 0.0f || imageWidth <= 0 || imageHeight <= 0)
    {
        return false;
    }

    const float u = (mousePos.x - imageMin.x) / width;
    const float v = (mousePos.y - imageMin.y) / height;
    imageX = static_cast<double>(u) * static_cast<double>(imageWidth);
    imageY = static_cast<double>(v) * static_cast<double>(imageHeight);
    return true;
}

float DistanceSquared(const ImVec2& a, const ImVec2& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

cv::Point2d ApplyTransformToPoint(const Transform2D& transform, double x, double y)
{
    const double outX = transform.matrix[0] * x + transform.matrix[1] * y + transform.matrix[2];
    const double outY = transform.matrix[3] * x + transform.matrix[4] * y + transform.matrix[5];
    return cv::Point2d(outX, outY);
}

cv::Point2d DisplayPointToLandmarkSpace(ViewerContent content,
                                        const cv::Point2d& displayPoint,
                                        bool useAlignment,
                                        const RegistrationResult* registration)
{
    if (content == ViewerContent::StackB && useAlignment && registration != nullptr)
    {
        return ApplyTransformToPoint(registration->inverse, displayPoint.x, displayPoint.y);
    }

    return displayPoint;
}
} // namespace

ViewerPanel::ViewerPanel(std::string title, ViewerContent content)
    : m_title(std::move(title))
    , m_content(content)
{
}

ZoomPanState ViewerPanel::GetZoomPanState(AppContext& context)
{
    if (context.session.uiPreferences.syncViewports)
    {
        return ZoomPanState{context.sharedZoom, context.sharedPanX, context.sharedPanY};
    }
    return ZoomPanState{m_zoom, m_panX, m_panY};
}

void ViewerPanel::ResetView(AppContext& context)
{
    ZoomPanState zoomPan = GetZoomPanState(context);
    zoomPan.zoom = 1.0f;
    zoomPan.panX = 0.0f;
    zoomPan.panY = 0.0f;
}

void ViewerPanel::DrawPreviewModeControls(AppContext& context)
{
    UiPreferences& prefs = context.session.uiPreferences;
    ImGui::PushID("preview_mode_controls");

    auto modeButton = [&](const char* label, PreviewMode mode)
    {
        const bool isActive = prefs.previewMode == mode;
        if (isActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.46f, 0.79f, 1.0f));
        }
        if (ImGui::Button(label))
        {
            prefs.previewMode = mode;
        }
        if (isActive)
        {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
    };

    modeButton("Blend", PreviewMode::Blend);
    modeButton("Checkerboard", PreviewMode::Checkerboard);
    modeButton("Difference", PreviewMode::Difference);
    modeButton("Multiply", PreviewMode::Multiply);
    ImGui::NewLine();

    if (prefs.previewMode == PreviewMode::Blend)
    {
        ImGui::SliderFloat("Blend Alpha", &prefs.blendAlpha, 0.0f, 1.0f, "%.2f");
    }
    else if (prefs.previewMode == PreviewMode::Checkerboard)
    {
        ImGui::SliderInt("Checker Size", &prefs.checkerSize, 4, 128);
    }
    ImGui::Checkbox("Sync Viewports (zoom/pan)", &prefs.syncViewports);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("When enabled, zooming/panning any of the 3 viewers moves all of them together.");
    }

    ImGui::PopID();
    ImGui::Separator();
}

void ViewerPanel::Draw(AppContext& context, const ImVec2& size)
{
    RefreshTexture(context);

    ImGui::BeginChild(m_title.c_str(), size, true);

    ImGui::TextUnformatted(m_title.c_str());
    ImGui::Separator();

    ImGui::PushID(m_title.c_str());
    ZoomPanState zoomPan = GetZoomPanState(context);
    if (ImGui::Button("Fit"))
    {
        ResetView(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1"))
    {
        zoomPan.zoom = m_lastFitScale > 0.0f ? 1.0f / m_lastFitScale : 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        ResetView(context);
    }
    ImGui::SameLine();
    ImGui::Text("Zoom %.2fx", zoomPan.zoom);
    ImGui::PopID();

    if (m_content == ViewerContent::Preview)
    {
        DrawPreviewModeControls(context);
    }

    if (m_content == ViewerContent::StackA)
    {
        DrawWindowLevelControls("wl_a", nullptr, GetActiveFixedStack(context.session));
    }
    else if (m_content == ViewerContent::StackB)
    {
        DrawWindowLevelControls("wl_b", nullptr, GetActiveMovingStack(context.session));
    }
    else
    {
        DrawWindowLevelControls("wl_pa", "Stack A window:", GetActiveFixedStack(context.session));
        DrawWindowLevelControls("wl_pb", "Stack B window:", GetActiveMovingStack(context.session));
    }

    ImGui::TextUnformatted("Mouse: wheel zoom, middle-button drag pan.");
    if (m_content != ViewerContent::Preview)
    {
        ImGui::TextUnformatted(context.landmarkModeEnabled
                                   ? "Landmarks: click to place/reposition, drag an existing point to adjust."
                                   : "Landmarks mode is off.");
    }
    ImGui::TextWrapped("%s", m_statusText.c_str());
    DrawImageCanvas(context, ImGui::GetContentRegionAvail());
    ImGui::EndChild();
}

Result ViewerPanel::LoadDisplayImage(AppContext& context, const StackModel& stack, const SliceRecord& slice, cv::Mat& outImage)
{
    const ImageLoadOptions options = MakeDisplayLoadOptions(stack, slice);
    if (!stack.isDicom)
    {
        return m_imageLoader.LoadColorImage(slice.filePath, outImage, options);
    }

    DicomLoader dicomLoader;
    const cv::Mat raw = context.dicomPixelCache.GetOrDecode(slice.filePath);
    if (raw.empty())
    {
        return Result{false, "Could not decode DICOM pixel data."};
    }

    outImage = dicomLoader.ApplyWindowLevel(raw, stack.rescaleSlope, stack.rescaleIntercept, stack.windowCenter, stack.windowWidth);
    if (outImage.empty())
    {
        return Result{false, "Could not remap DICOM pixel data for display."};
    }
    ApplyOrientation(outImage, options);
    return Result{};
}

void ViewerPanel::RefreshTexture(AppContext& context)
{
    cv::Mat image;
    const SliceRecord* sliceA = GetActiveSlice(GetActiveFixedStack(context.session), GetActivePairing(context.session).activeFixedIndex);
    const SliceRecord* sliceB = GetActiveSlice(GetActiveMovingStack(context.session), GetActivePairing(context.session).activeMovingIndex);
    const RegistrationResult* currentRegistration = FindCurrentRegistration(context);
    RegistrationResult previewHistorySelection;
    const RegistrationResult* registration =
        m_content == ViewerContent::Preview
            ? ResolvePreviewRegistration(context, currentRegistration, previewHistorySelection)
            : currentRegistration;
    const bool useAlignmentPreview = context.session.projectPreferences.useAlignmentPreview &&
                                     HasUsableTransform(registration);
    double currentTx = 0.0;
    double currentTy = 0.0;
    double currentTheta = 0.0;
    double currentScale = 1.0;
    ExtractTransformParameters(registration, currentTx, currentTy, currentTheta, currentScale);

    if (m_content == ViewerContent::StackA)
    {
        const SliceRecord* slice = sliceA;
        if (slice == nullptr || slice->filePath.empty())
        {
            m_texture.Reset();
            m_statusText = "Stack A reference: no slice loaded.";
            return;
        }

        if (m_loadedFilePath == slice->filePath &&
            m_loadedWindowCenterA == GetActiveFixedStack(context.session).windowCenter &&
            m_loadedWindowWidthA == GetActiveFixedStack(context.session).windowWidth &&
            m_loadedFlipHA == slice->flipHorizontal && m_loadedFlipVA == slice->flipVertical &&
            m_loadedRotationA == slice->rotationDegrees)
        {
            m_statusText = "Stack A reference: " + slice->fileName;
            return;
        }

        Result result = LoadDisplayImage(context, GetActiveFixedStack(context.session), *slice, image);
        if (!result.ok)
        {
            m_texture.Reset();
            m_statusText = "Stack A load failed: " + result.message;
            m_loadedFilePath.clear();
            return;
        }

        m_texture.Upload(image);
        m_loadedFilePath = slice->filePath;
        m_loadedUsedAlignment = false;
        m_loadedWindowCenterA = GetActiveFixedStack(context.session).windowCenter;
        m_loadedWindowWidthA = GetActiveFixedStack(context.session).windowWidth;
        m_loadedFlipHA = slice->flipHorizontal;
        m_loadedFlipVA = slice->flipVertical;
        m_loadedRotationA = slice->rotationDegrees;
        m_statusText = "Stack A reference: " + slice->fileName;
        return;
    }

    if (m_content == ViewerContent::StackB)
    {
        const SliceRecord* slice = sliceB;
        if (slice == nullptr || slice->filePath.empty())
        {
            m_texture.Reset();
            m_statusText = "Stack B moving: no slice loaded.";
            return;
        }

        if (m_loadedFilePath == slice->filePath &&
            m_loadedTx == currentTx && m_loadedTy == currentTy && m_loadedTheta == currentTheta &&
            m_loadedScale == currentScale &&
            m_loadedWindowCenterB == GetActiveMovingStack(context.session).windowCenter &&
            m_loadedWindowWidthB == GetActiveMovingStack(context.session).windowWidth &&
            m_loadedFlipHB == slice->flipHorizontal && m_loadedFlipVB == slice->flipVertical &&
            m_loadedRotationB == slice->rotationDegrees)
        {
            m_statusText = "Stack B original: " + slice->fileName;
            return;
        }

        Result result = LoadDisplayImage(context, GetActiveMovingStack(context.session), *slice, image);
        if (!result.ok)
        {
            m_texture.Reset();
            m_statusText = "Stack B load failed: " + result.message;
            m_loadedFilePath.clear();
            return;
        }

        m_texture.Upload(image);
        m_loadedFilePath = slice->filePath;
        m_loadedUsedAlignment = false;
        m_loadedTx = currentTx;
        m_loadedTy = currentTy;
        m_loadedTheta = currentTheta;
        m_loadedScale = currentScale;
        m_loadedWindowCenterB = GetActiveMovingStack(context.session).windowCenter;
        m_loadedWindowWidthB = GetActiveMovingStack(context.session).windowWidth;
        m_loadedFlipHB = slice->flipHorizontal;
        m_loadedFlipVB = slice->flipVertical;
        m_loadedRotationB = slice->rotationDegrees;
        m_statusText = "Stack B original: " + slice->fileName;
        return;
    }

    if (sliceA == nullptr || sliceB == nullptr || sliceA->filePath.empty() || sliceB->filePath.empty())
    {
        m_texture.Reset();
        m_statusText = "Preview: load both stacks to composite.";
        m_loadedSliceA = -1;
        m_loadedSliceB = -1;
        return;
    }

    const bool previewConfigUnchanged =
        m_loadedPreviewMode == context.session.uiPreferences.previewMode &&
        m_loadedBlendAlpha == context.session.uiPreferences.blendAlpha &&
        m_loadedCheckerSize == context.session.uiPreferences.checkerSize;
    const double registrationScore = registration != nullptr ? registration->score : -1.0;

    if (m_loadedSliceA == sliceA->stackIndex && m_loadedSliceB == sliceB->stackIndex && previewConfigUnchanged &&
        m_loadedUsedAlignment == useAlignmentPreview && m_loadedRegistrationScore == registrationScore &&
        m_loadedTx == currentTx && m_loadedTy == currentTy && m_loadedTheta == currentTheta &&
        m_loadedScale == currentScale &&
        m_loadedWindowCenterA == GetActiveFixedStack(context.session).windowCenter && m_loadedWindowWidthA == GetActiveFixedStack(context.session).windowWidth &&
        m_loadedWindowCenterB == GetActiveMovingStack(context.session).windowCenter && m_loadedWindowWidthB == GetActiveMovingStack(context.session).windowWidth &&
        m_loadedFlipHA == sliceA->flipHorizontal && m_loadedFlipVA == sliceA->flipVertical && m_loadedRotationA == sliceA->rotationDegrees &&
        m_loadedFlipHB == sliceB->flipHorizontal && m_loadedFlipVB == sliceB->flipVertical && m_loadedRotationB == sliceB->rotationDegrees)
    {
        m_statusText = "Preview: " + sliceA->fileName + " vs " + sliceB->fileName;
        return;
    }

    cv::Mat imageA;
    cv::Mat imageB;
    Result loadA = LoadDisplayImage(context, GetActiveFixedStack(context.session), *sliceA, imageA);
    Result loadB = LoadDisplayImage(context, GetActiveMovingStack(context.session), *sliceB, imageB);
    if (!loadA.ok || !loadB.ok)
    {
        m_texture.Reset();
        m_statusText = "Preview load failed.";
        m_loadedSliceA = -1;
        m_loadedSliceB = -1;
        return;
    }

    cv::Mat movingForPreview = imageB;
    if (useAlignmentPreview)
    {
        movingForPreview = ApplyTransformToMoving(imageB, imageA.size(), registration->forward);
    }

    cv::Mat preview = BuildPreviewImage(imageA, movingForPreview, context.session.uiPreferences);
    if (preview.empty())
    {
        m_texture.Reset();
        m_statusText = "Preview could not be generated.";
        return;
    }

    m_texture.Upload(preview);
    m_loadedSliceA = sliceA->stackIndex;
    m_loadedSliceB = sliceB->stackIndex;
    m_loadedPreviewMode = context.session.uiPreferences.previewMode;
    m_loadedBlendAlpha = context.session.uiPreferences.blendAlpha;
    m_loadedCheckerSize = context.session.uiPreferences.checkerSize;
    m_loadedUsedAlignment = useAlignmentPreview;
    m_loadedRegistrationScore = registrationScore;
    m_loadedTx = currentTx;
    m_loadedTy = currentTy;
    m_loadedTheta = currentTheta;
    m_loadedScale = currentScale;
    m_loadedWindowCenterA = GetActiveFixedStack(context.session).windowCenter;
    m_loadedWindowWidthA = GetActiveFixedStack(context.session).windowWidth;
    m_loadedWindowCenterB = GetActiveMovingStack(context.session).windowCenter;
    m_loadedWindowWidthB = GetActiveMovingStack(context.session).windowWidth;
    m_loadedFlipHA = sliceA->flipHorizontal;
    m_loadedFlipVA = sliceA->flipVertical;
    m_loadedRotationA = sliceA->rotationDegrees;
    m_loadedFlipHB = sliceB->flipHorizontal;
    m_loadedFlipVB = sliceB->flipVertical;
    m_loadedRotationB = sliceB->rotationDegrees;
    m_loadedFilePath.clear();
    m_statusText = "Preview: " + sliceA->fileName + " vs " + sliceB->fileName;
}

void ViewerPanel::DrawImageCanvas(AppContext& context, const ImVec2& canvasSize)
{
    const ImVec2 canvasStart = ImGui::GetCursorScreenPos();
    const ImVec2 canvasEnd(canvasStart.x + canvasSize.x, canvasStart.y + canvasSize.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasStart, canvasEnd, IM_COL32(28, 31, 38, 255));
    drawList->AddRect(canvasStart, canvasEnd, IM_COL32(70, 76, 88, 255));

    ImGui::SetCursorScreenPos(canvasStart);
    ImGui::InvisibleButton((m_title + "_canvas").c_str(), canvasSize);
    const bool canvasHovered = ImGui::IsItemHovered();

    if (!m_texture.IsValid())
    {
        drawList->AddText(ImVec2(canvasStart.x + 12.0f, canvasStart.y + 12.0f), IM_COL32(220, 225, 232, 255),
                          "No image");
        return;
    }

    ZoomPanState zoomPan = GetZoomPanState(context);
    const float baseWidth = static_cast<float>(m_texture.GetWidth());
    const float baseHeight = static_cast<float>(m_texture.GetHeight());
    const float fitScale = (std::min)(canvasSize.x / baseWidth, canvasSize.y / baseHeight);
    m_lastFitScale = fitScale > 0.0f ? fitScale : 1.0f;
    const float drawWidth = baseWidth * m_lastFitScale * zoomPan.zoom;
    const float drawHeight = baseHeight * m_lastFitScale * zoomPan.zoom;

    const ImVec2 center(canvasStart.x + canvasSize.x * 0.5f + zoomPan.panX, canvasStart.y + canvasSize.y * 0.5f + zoomPan.panY);
    const ImVec2 min(center.x - drawWidth * 0.5f, center.y - drawHeight * 0.5f);
    const ImVec2 max(center.x + drawWidth * 0.5f, center.y + drawHeight * 0.5f);

    drawList->PushClipRect(canvasStart, canvasEnd, true);
    drawList->AddImage(reinterpret_cast<void*>(static_cast<intptr_t>(m_texture.GetId())), min, max);

    if (canvasHovered)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float zoomFactor = wheel > 0.0f ? 1.1f : 0.9f;
            zoomPan.zoom = (std::clamp)(zoomPan.zoom * zoomFactor, 0.05f, 20.0f);
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            zoomPan.panX += delta.x;
            zoomPan.panY += delta.y;
        }

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
        {
            ResetView(context);
        }
    }

    RegistrationResult* editableRegistration = context.landmarkModeEnabled ? GetOrCreateCurrentRegistration(context) : nullptr;
    const RegistrationResult* currentRegistration = FindCurrentRegistration(context);
    const RegistrationResult* displayedRegistration =
        editableRegistration != nullptr ? editableRegistration : currentRegistration;
    const bool useAlignment = m_content == ViewerContent::Preview &&
                              context.session.projectPreferences.useAlignmentPreview &&
                              HasUsableTransform(currentRegistration);
    int hoveredLandmarkIndex = -1;
    const float kHitRadius = 10.0f * context.uiScale;

    if (displayedRegistration != nullptr && m_content != ViewerContent::Preview)
    {
        for (int i = 0; i < static_cast<int>(displayedRegistration->landmarks.size()); ++i)
        {
            const LandmarkPair& landmark = displayedRegistration->landmarks[i];
            double pointX = 0.0;
            double pointY = 0.0;

            if (m_content == ViewerContent::StackB)
            {
                if (!HasMovingPoint(landmark))
                {
                    continue;
                }
                pointX = landmark.movingX;
                pointY = landmark.movingY;

                if (useAlignment && currentRegistration != nullptr)
                {
                    const cv::Point2d transformed = ApplyTransformToPoint(currentRegistration->forward, pointX, pointY);
                    pointX = transformed.x;
                    pointY = transformed.y;
                }
            }
            else
            {
                if (!HasFixedPoint(landmark))
                {
                    continue;
                }
                pointX = landmark.fixedX;
                pointY = landmark.fixedY;
            }

            const ImVec2 screenPoint =
                ToScreenPoint(pointX, pointY, min, max, m_texture.GetWidth(), m_texture.GetHeight());
            const bool isSelected = context.landmarkEditState.selectedIndex == i &&
                                    ((m_content == ViewerContent::StackB &&
                                      context.landmarkEditState.target == LandmarkEditTarget::Moving) ||
                                     (m_content == ViewerContent::StackA &&
                                      context.landmarkEditState.target == LandmarkEditTarget::Fixed));
            const float distanceSq = DistanceSquared(ImGui::GetIO().MousePos, screenPoint);
            if (distanceSq <= kHitRadius * kHitRadius)
            {
                hoveredLandmarkIndex = i;
            }

            drawList->AddCircleFilled(screenPoint, (isSelected ? 7.0f : 5.0f) * context.uiScale,
                                      isSelected ? IM_COL32(255, 110, 80, 255) : IM_COL32(250, 190, 60, 255));
            drawList->AddText(ImVec2(screenPoint.x + 6.0f * context.uiScale, screenPoint.y - 6.0f * context.uiScale),
                              IM_COL32(255, 245, 210, 255), std::to_string(i + 1).c_str());
        }
    }

    if (editableRegistration != nullptr && m_content != ViewerContent::Preview && canvasHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredLandmarkIndex >= 0)
    {
        context.landmarkEditState.selectedIndex = hoveredLandmarkIndex;
        context.landmarkEditState.isDragging = true;
        context.landmarkEditState.target =
            m_content == ViewerContent::StackB ? LandmarkEditTarget::Moving : LandmarkEditTarget::Fixed;
    }

    if (editableRegistration != nullptr && context.landmarkEditState.isDragging &&
        ((m_content == ViewerContent::StackB && context.landmarkEditState.target == LandmarkEditTarget::Moving) ||
         (m_content == ViewerContent::StackA && context.landmarkEditState.target == LandmarkEditTarget::Fixed)) &&
        context.landmarkEditState.selectedIndex >= 0 &&
        context.landmarkEditState.selectedIndex < static_cast<int>(editableRegistration->landmarks.size()))
    {
        double imageX = 0.0;
        double imageY = 0.0;
        if (TryMapMouseToImage(ImGui::GetIO().MousePos, min, max, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
        {
            const cv::Point2d landmarkPoint =
                DisplayPointToLandmarkSpace(m_content, cv::Point2d(imageX, imageY), useAlignment, currentRegistration);
            LandmarkPair& landmark = editableRegistration->landmarks[context.landmarkEditState.selectedIndex];
            if (m_content == ViewerContent::StackB)
            {
                landmark.movingX = landmarkPoint.x;
                landmark.movingY = landmarkPoint.y;
            }
            else
            {
                landmark.fixedX = landmarkPoint.x;
                landmark.fixedY = landmarkPoint.y;
            }

            UpdateManualPreviewRegistration(*editableRegistration);
        }
    }

    if (context.landmarkEditState.isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        context.landmarkEditState.isDragging = false;
    }

    if (context.landmarkModeEnabled &&
        m_content != ViewerContent::Preview && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        hoveredLandmarkIndex < 0)
    {
        double imageX = 0.0;
        double imageY = 0.0;
        if (TryMapMouseToImage(ImGui::GetIO().MousePos, min, max, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
        {
            const cv::Point2d landmarkPoint =
                DisplayPointToLandmarkSpace(m_content, cv::Point2d(imageX, imageY), useAlignment, currentRegistration);
            if (editableRegistration != nullptr)
            {
                const bool needMovingPoint = m_content == ViewerContent::StackB;

                // Order-independent placement: complete the currently selected landmark if it's
                // still missing this side; otherwise reuse any other open landmark missing this
                // side; otherwise start a brand-new landmark. A click never touches a landmark
                // that already has a point on the clicked side, so repeated clicks on the same
                // stack always create new landmarks instead of repositioning a finished one.
                int targetIndex = -1;
                const bool hasSelectedLandmark =
                    context.landmarkEditState.selectedIndex >= 0 &&
                    context.landmarkEditState.selectedIndex < static_cast<int>(editableRegistration->landmarks.size());
                if (hasSelectedLandmark)
                {
                    const LandmarkPair& selected = editableRegistration->landmarks[context.landmarkEditState.selectedIndex];
                    const bool missingThisSide = needMovingPoint ? !HasMovingPoint(selected) : !HasFixedPoint(selected);
                    if (missingThisSide)
                    {
                        targetIndex = context.landmarkEditState.selectedIndex;
                    }
                }

                if (targetIndex < 0)
                {
                    targetIndex = FindOpenLandmarkIndex(*editableRegistration, needMovingPoint);
                }

                if (targetIndex < 0)
                {
                    LandmarkPair landmark;
                    landmark.movingX = std::numeric_limits<double>::quiet_NaN();
                    landmark.movingY = std::numeric_limits<double>::quiet_NaN();
                    landmark.fixedX = std::numeric_limits<double>::quiet_NaN();
                    landmark.fixedY = std::numeric_limits<double>::quiet_NaN();
                    editableRegistration->landmarks.push_back(landmark);
                    targetIndex = static_cast<int>(editableRegistration->landmarks.size()) - 1;
                }

                LandmarkPair& landmark = editableRegistration->landmarks[targetIndex];
                if (needMovingPoint)
                {
                    landmark.movingX = landmarkPoint.x;
                    landmark.movingY = landmarkPoint.y;
                    context.landmarkEditState.target = LandmarkEditTarget::Moving;
                }
                else
                {
                    landmark.fixedX = landmarkPoint.x;
                    landmark.fixedY = landmarkPoint.y;
                    context.landmarkEditState.target = LandmarkEditTarget::Fixed;
                }
                context.landmarkEditState.selectedIndex = targetIndex;
                UpdateManualPreviewRegistration(*editableRegistration);
            }
        }
    }

    if (editableRegistration != nullptr && context.landmarkEditState.selectedIndex >= 0 &&
        context.landmarkEditState.selectedIndex < static_cast<int>(editableRegistration->landmarks.size()) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        editableRegistration->landmarks.erase(editableRegistration->landmarks.begin() + context.landmarkEditState.selectedIndex);
        context.landmarkEditState.selectedIndex = -1;
        context.landmarkEditState.target = LandmarkEditTarget::None;
        context.landmarkEditState.isDragging = false;
        UpdateManualPreviewRegistration(*editableRegistration);
    }

    if (context.landmarkModeEnabled && m_content != ViewerContent::Preview && canvasHovered)
    {
        if (hoveredLandmarkIndex >= 0)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        else
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float kCrosshairSize = 10.0f * context.uiScale;
            const float kCrosshairGap = 3.0f * context.uiScale;
            const float kCrosshairThickness = 1.5f * context.uiScale;
            const ImU32 crosshairColor = IM_COL32(250, 220, 60, 255);
            drawList->AddLine(ImVec2(mouse.x - kCrosshairSize, mouse.y), ImVec2(mouse.x - kCrosshairGap, mouse.y), crosshairColor, kCrosshairThickness);
            drawList->AddLine(ImVec2(mouse.x + kCrosshairGap, mouse.y), ImVec2(mouse.x + kCrosshairSize, mouse.y), crosshairColor, kCrosshairThickness);
            drawList->AddLine(ImVec2(mouse.x, mouse.y - kCrosshairSize), ImVec2(mouse.x, mouse.y - kCrosshairGap), crosshairColor, kCrosshairThickness);
            drawList->AddLine(ImVec2(mouse.x, mouse.y + kCrosshairGap), ImVec2(mouse.x, mouse.y + kCrosshairSize), crosshairColor, kCrosshairThickness);
            drawList->AddCircle(mouse, kCrosshairGap, crosshairColor, 0, kCrosshairThickness);
        }
    }

    if (m_content == ViewerContent::Preview && context.transposeModeEnabled)
    {
        DrawTransposeGizmo(context, drawList, min, max, canvasHovered);
    }

    drawList->PopClipRect();
}

void ViewerPanel::DrawTransposeGizmo(AppContext& context, ImDrawList* drawList, const ImVec2& imageMin, const ImVec2& imageMax, bool canvasHovered)
{
    TransposeState& tp = context.transposeState;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float handleRadius = 8.0f * context.uiScale;
    const float hitRadius = handleRadius * 2.0f;
    const ImU32 lineColor = IM_COL32(80, 220, 255, 255);
    const ImU32 moveColor = IM_COL32(255, 210, 60, 255);
    const ImU32 rotateScaleColor = IM_COL32(255, 120, 200, 255);

    if (!tp.hasActionLine)
    {
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            double imageX = 0.0, imageY = 0.0;
            if (TryMapMouseToImage(mouse, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
            {
                tp.pivotX = imageX;
                tp.pivotY = imageY;
                tp.controlX = imageX;
                tp.controlY = imageY;
                tp.isPlacingActionLine = true;
            }
        }

        if (tp.isPlacingActionLine)
        {
            double imageX = 0.0, imageY = 0.0;
            if (TryMapMouseToImage(mouse, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
            {
                tp.controlX = imageX;
                tp.controlY = imageY;
            }

            const ImVec2 pivotScreen = ToScreenPoint(tp.pivotX, tp.pivotY, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight());
            const ImVec2 controlScreen = ToScreenPoint(tp.controlX, tp.controlY, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight());
            drawList->AddLine(pivotScreen, controlScreen, lineColor, 2.0f);
            drawList->AddCircle(pivotScreen, handleRadius, moveColor, 0, 2.0f);

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                tp.isPlacingActionLine = false;
                const double dx = tp.controlX - tp.pivotX;
                const double dy = tp.controlY - tp.pivotY;
                if (std::sqrt(dx * dx + dy * dy) > 4.0)
                {
                    tp.hasActionLine = true;
                }
            }
        }
        return;
    }

    RegistrationResult* registration = GetOrCreateCurrentRegistration(context);

    const ImVec2 pivotScreen = ToScreenPoint(tp.pivotX, tp.pivotY, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight());
    const ImVec2 controlScreen = ToScreenPoint(tp.controlX, tp.controlY, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight());
    const ImVec2 middleScreen((pivotScreen.x + controlScreen.x) * 0.5f, (pivotScreen.y + controlScreen.y) * 0.5f);

    drawList->AddLine(pivotScreen, controlScreen, lineColor, 2.0f);
    drawList->AddCircle(pivotScreen, handleRadius, moveColor, 0, 2.0f);
    drawList->AddCircle(middleScreen, handleRadius, moveColor, 0, 2.0f);
    drawList->AddCircle(controlScreen, handleRadius, rotateScaleColor, 0, 2.0f);

    if (canvasHovered && tp.activeDragZone == TransposeZone::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        double imageX = 0.0, imageY = 0.0;
        if (TryMapMouseToImage(mouse, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
        {
            if (DistanceSquared(mouse, pivotScreen) <= hitRadius * hitRadius ||
                DistanceSquared(mouse, middleScreen) <= hitRadius * hitRadius)
            {
                tp.activeDragZone = TransposeZone::Move;
            }
            else if (DistanceSquared(mouse, controlScreen) <= hitRadius * hitRadius)
            {
                tp.activeDragZone = TransposeZone::RotateScale;
            }

            if (tp.activeDragZone != TransposeZone::None)
            {
                tp.dragStartPivotX = tp.pivotX;
                tp.dragStartPivotY = tp.pivotY;
                tp.dragStartControlX = tp.controlX;
                tp.dragStartControlY = tp.controlY;
                tp.dragStartMouseImageX = imageX;
                tp.dragStartMouseImageY = imageY;
                tp.dragStartAdjustment = registration != nullptr ? registration->manualAdjustment : Transform2D{};
            }
        }
    }

    if (tp.activeDragZone != TransposeZone::None)
    {
        double imageX = 0.0, imageY = 0.0;
        if (registration != nullptr && TryMapMouseToImage(mouse, imageMin, imageMax, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
        {
            Transform2D frameDelta;
            if (tp.activeDragZone == TransposeZone::Move)
            {
                // Dragging the pivot translates the whole image; pivot and control point move together.
                const double dx = imageX - tp.dragStartMouseImageX;
                const double dy = imageY - tp.dragStartMouseImageY;
                frameDelta = MakeTranslation2D(dx, dy);
                tp.pivotX = tp.dragStartPivotX + dx;
                tp.pivotY = tp.dragStartPivotY + dy;
                tp.controlX = tp.dragStartControlX + dx;
                tp.controlY = tp.dragStartControlY + dy;
            }
            else // RotateScale: distance from pivot drives scale, angle around pivot drives rotation -- both at once.
            {
                const double startDx = tp.dragStartControlX - tp.dragStartPivotX;
                const double startDy = tp.dragStartControlY - tp.dragStartPivotY;
                const double startDist = std::sqrt(startDx * startDx + startDy * startDy);
                const double currentDx = imageX - tp.dragStartPivotX;
                const double currentDy = imageY - tp.dragStartPivotY;
                const double currentDist = std::sqrt(currentDx * currentDx + currentDy * currentDy);

                const double factor = startDist > 1e-6 ? currentDist / startDist : 1.0;
                const double startAngle = std::atan2(startDy, startDx);
                const double currentAngle = std::atan2(currentDy, currentDx);
                const double deltaAngle = currentAngle - startAngle;

                frameDelta = MakeRotateScaleAround2D(tp.dragStartPivotX, tp.dragStartPivotY, deltaAngle, factor);
                const cv::Point2d newControl = ApplyTransformToPoint(frameDelta, tp.dragStartControlX, tp.dragStartControlY);
                tp.controlX = newControl.x;
                tp.controlY = newControl.y;
            }

            registration->manualAdjustment = ComposeTransform2D(frameDelta, tp.dragStartAdjustment);
            registration->hasManualAdjustment = true;
            registration->forward = ComposeTransform2D(registration->manualAdjustment, registration->baseForward);
            registration->inverse = InvertTransform2D(registration->forward);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            tp.activeDragZone = TransposeZone::None;
        }
    }
}
} // namespace align
