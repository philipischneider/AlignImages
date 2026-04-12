#include "viewer/ViewerPanel.h"

#include "imgui.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace align
{
namespace
{
RegistrationResult* GetOrCreateCurrentRegistration(AppContext& context)
{
    const int activeSliceA = context.session.projectPreferences.activeSliceA;
    if (activeSliceA < 0 || activeSliceA >= static_cast<int>(context.session.pairing.pairs.size()))
    {
        return nullptr;
    }

    const PairRecord& pair = context.session.pairing.pairs[activeSliceA];
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

    if (!registration->iterations.empty())
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

void ViewerPanel::ResetView()
{
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
}

void ViewerPanel::Draw(AppContext& context, const ImVec2& size)
{
    RefreshTexture(context);

    ImGui::BeginChild(m_title.c_str(), size, true);

    ImGui::TextUnformatted(m_title.c_str());
    ImGui::Separator();

    ImGui::PushID(m_title.c_str());
    if (ImGui::Button("Fit"))
    {
        ResetView();
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1"))
    {
        m_zoom = 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        ResetView();
    }
    ImGui::SameLine();
    ImGui::Text("Zoom %.2fx", m_zoom);
    ImGui::PopID();

    ImGui::TextUnformatted("Mouse: wheel zoom, middle-button drag pan.");
    ImGui::TextWrapped("%s", m_statusText.c_str());
    DrawImageCanvas(context, ImGui::GetContentRegionAvail());
    ImGui::EndChild();
}

void ViewerPanel::RefreshTexture(AppContext& context)
{
    cv::Mat image;
    const SliceRecord* sliceA = GetActiveSlice(context.session.stackA, context.session.projectPreferences.activeSliceA);
    const SliceRecord* sliceB = GetActiveSlice(context.session.stackB, context.session.projectPreferences.activeSliceB);
    const RegistrationResult* registration =
        (sliceA != nullptr && sliceB != nullptr)
            ? FindRegistrationResult(context.session.registrations, sliceA->stackIndex, sliceB->stackIndex)
            : nullptr;
    const bool useAlignment = context.session.projectPreferences.useAlignmentPreview && registration != nullptr;
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

        if (m_loadedFilePath == slice->filePath)
        {
            m_statusText = "Stack A reference: " + slice->fileName;
            return;
        }

        Result result = m_imageLoader.LoadColorImage(slice->filePath, image);
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

        if (m_loadedFilePath == slice->filePath && m_loadedUsedAlignment == useAlignment &&
            m_loadedTx == currentTx && m_loadedTy == currentTy && m_loadedTheta == currentTheta &&
            m_loadedScale == currentScale)
        {
            m_statusText = useAlignment ? "Stack B aligned: " + slice->fileName : "Stack B moving: " + slice->fileName;
            return;
        }

        Result result = m_imageLoader.LoadColorImage(slice->filePath, image);
        if (!result.ok)
        {
            m_texture.Reset();
            m_statusText = "Stack B load failed: " + result.message;
            m_loadedFilePath.clear();
            return;
        }

        cv::Mat displayImage = image;
        if (useAlignment)
        {
            const cv::Size targetSize =
                (sliceA != nullptr && sliceA->width > 0 && sliceA->height > 0) ? cv::Size(sliceA->width, sliceA->height)
                                                                                : image.size();
            displayImage = ApplyTransformToMoving(image, targetSize, registration->forward);
        }

        m_texture.Upload(displayImage);
        m_loadedFilePath = slice->filePath;
        m_loadedUsedAlignment = useAlignment;
        m_loadedTx = currentTx;
        m_loadedTy = currentTy;
        m_loadedTheta = currentTheta;
        m_loadedScale = currentScale;
        m_statusText = useAlignment ? "Stack B aligned: " + slice->fileName : "Stack B moving: " + slice->fileName;
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
        m_loadedUsedAlignment == useAlignment && m_loadedRegistrationScore == registrationScore &&
        m_loadedTx == currentTx && m_loadedTy == currentTy && m_loadedTheta == currentTheta &&
        m_loadedScale == currentScale)
    {
        m_statusText = "Preview: " + sliceA->fileName + " vs " + sliceB->fileName;
        return;
    }

    cv::Mat imageA;
    cv::Mat imageB;
    Result loadA = m_imageLoader.LoadColorImage(sliceA->filePath, imageA);
    Result loadB = m_imageLoader.LoadColorImage(sliceB->filePath, imageB);
    if (!loadA.ok || !loadB.ok)
    {
        m_texture.Reset();
        m_statusText = "Preview load failed.";
        m_loadedSliceA = -1;
        m_loadedSliceB = -1;
        return;
    }

    cv::Mat movingForPreview = imageB;
    if (useAlignment)
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
    m_loadedUsedAlignment = useAlignment;
    m_loadedRegistrationScore = registrationScore;
    m_loadedTx = currentTx;
    m_loadedTy = currentTy;
    m_loadedTheta = currentTheta;
    m_loadedScale = currentScale;
    m_loadedFilePath.clear();
    m_statusText = "Preview: " + sliceA->fileName + " vs " + sliceB->fileName;
}

void ViewerPanel::DrawImageCanvas(AppContext& context, const ImVec2& canvasSize)
{
    const ImVec2 canvasStart = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasStart, ImVec2(canvasStart.x + canvasSize.x, canvasStart.y + canvasSize.y),
                            IM_COL32(28, 31, 38, 255));
    drawList->AddRect(canvasStart, ImVec2(canvasStart.x + canvasSize.x, canvasStart.y + canvasSize.y),
                      IM_COL32(70, 76, 88, 255));

    if (!m_texture.IsValid())
    {
        drawList->AddText(ImVec2(canvasStart.x + 12.0f, canvasStart.y + 12.0f), IM_COL32(220, 225, 232, 255),
                          "No image");
        ImGui::Dummy(canvasSize);
        return;
    }

    const float baseWidth = static_cast<float>(m_texture.GetWidth());
    const float baseHeight = static_cast<float>(m_texture.GetHeight());
    const float aspect = baseWidth / baseHeight;
    float drawWidth = baseWidth * m_zoom;
    float drawHeight = baseHeight * m_zoom;

    const float fitScale = (std::min)(canvasSize.x / baseWidth, canvasSize.y / baseHeight);
    if (drawWidth > canvasSize.x * 4.0f || drawHeight > canvasSize.y * 4.0f)
    {
        drawWidth = baseWidth * fitScale * m_zoom;
        drawHeight = drawWidth / aspect;
    }

    if (drawHeight > canvasSize.y * 4.0f)
    {
        drawHeight = baseHeight * fitScale * m_zoom;
        drawWidth = drawHeight * aspect;
    }

    const ImVec2 center(canvasStart.x + canvasSize.x * 0.5f + m_panX, canvasStart.y + canvasSize.y * 0.5f + m_panY);
    const ImVec2 min(center.x - drawWidth * 0.5f, center.y - drawHeight * 0.5f);
    const ImVec2 max(center.x + drawWidth * 0.5f, center.y + drawHeight * 0.5f);

    ImGui::SetCursorScreenPos(min);
    ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(m_texture.GetId())), ImVec2(drawWidth, drawHeight));
    const bool imageHovered = ImGui::IsItemHovered();

    if (imageHovered)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float zoomFactor = wheel > 0.0f ? 1.1f : 0.9f;
            m_zoom = (std::clamp)(m_zoom * zoomFactor, 0.05f, 20.0f);
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_panX += delta.x;
            m_panY += delta.y;
        }

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
        {
            ResetView();
        }
    }

    RegistrationResult* registration = GetOrCreateCurrentRegistration(context);
    const RegistrationResult* currentRegistration = FindRegistrationResult(
        context.session.registrations,
        context.session.projectPreferences.activeSliceA,
        context.session.projectPreferences.activeSliceB);
    const bool useAlignment = context.session.projectPreferences.useAlignmentPreview && currentRegistration != nullptr;
    int hoveredLandmarkIndex = -1;
    constexpr float kHitRadius = 10.0f;

    if (registration != nullptr && m_content != ViewerContent::Preview)
    {
        for (int i = 0; i < static_cast<int>(registration->landmarks.size()); ++i)
        {
            LandmarkPair& landmark = registration->landmarks[i];
            double pointX = 0.0;
            double pointY = 0.0;

            if (m_content == ViewerContent::StackB)
            {
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

            drawList->AddCircleFilled(screenPoint, isSelected ? 7.0f : 5.0f,
                                      isSelected ? IM_COL32(255, 110, 80, 255) : IM_COL32(250, 190, 60, 255));
            drawList->AddText(ImVec2(screenPoint.x + 6.0f, screenPoint.y - 6.0f), IM_COL32(255, 245, 210, 255),
                              std::to_string(i + 1).c_str());
        }
    }

    if (registration != nullptr && m_content != ViewerContent::Preview && imageHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredLandmarkIndex >= 0)
    {
        context.landmarkEditState.selectedIndex = hoveredLandmarkIndex;
        context.landmarkEditState.isDragging = true;
        context.landmarkEditState.target =
            m_content == ViewerContent::StackB ? LandmarkEditTarget::Moving : LandmarkEditTarget::Fixed;
    }

    if (registration != nullptr && context.landmarkEditState.isDragging &&
        ((m_content == ViewerContent::StackB && context.landmarkEditState.target == LandmarkEditTarget::Moving) ||
         (m_content == ViewerContent::StackA && context.landmarkEditState.target == LandmarkEditTarget::Fixed)) &&
        context.landmarkEditState.selectedIndex >= 0 &&
        context.landmarkEditState.selectedIndex < static_cast<int>(registration->landmarks.size()))
    {
        double imageX = 0.0;
        double imageY = 0.0;
        if (TryMapMouseToImage(ImGui::GetIO().MousePos, min, max, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
        {
            const cv::Point2d landmarkPoint =
                DisplayPointToLandmarkSpace(m_content, cv::Point2d(imageX, imageY), useAlignment, currentRegistration);
            LandmarkPair& landmark = registration->landmarks[context.landmarkEditState.selectedIndex];
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
        }
    }

    if (context.landmarkEditState.isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        context.landmarkEditState.isDragging = false;
    }

    if (m_content != ViewerContent::Preview && imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        hoveredLandmarkIndex < 0)
    {
        double imageX = 0.0;
        double imageY = 0.0;
        if (TryMapMouseToImage(ImGui::GetIO().MousePos, min, max, m_texture.GetWidth(), m_texture.GetHeight(), imageX, imageY))
        {
            const cv::Point2d landmarkPoint =
                DisplayPointToLandmarkSpace(m_content, cv::Point2d(imageX, imageY), useAlignment, currentRegistration);
            RegistrationResult* editableRegistration = GetOrCreateCurrentRegistration(context);
            if (editableRegistration != nullptr)
            {
                if (m_content == ViewerContent::StackB)
                {
                    context.pendingLandmarkPoint.hasMovingPoint = true;
                    context.pendingLandmarkPoint.movingX = landmarkPoint.x;
                    context.pendingLandmarkPoint.movingY = landmarkPoint.y;
                    context.landmarkEditState.selectedIndex = -1;
                    context.landmarkEditState.target = LandmarkEditTarget::None;
                }
                else if (m_content == ViewerContent::StackA && context.pendingLandmarkPoint.hasMovingPoint)
                {
                    LandmarkPair landmark;
                    landmark.movingX = context.pendingLandmarkPoint.movingX;
                    landmark.movingY = context.pendingLandmarkPoint.movingY;
                    landmark.fixedX = landmarkPoint.x;
                    landmark.fixedY = landmarkPoint.y;
                    editableRegistration->landmarks.push_back(landmark);
                    context.pendingLandmarkPoint.hasMovingPoint = false;
                    context.landmarkEditState.selectedIndex = static_cast<int>(editableRegistration->landmarks.size()) - 1;
                    context.landmarkEditState.target = LandmarkEditTarget::Fixed;
                }
            }
        }
    }

    if (registration != nullptr && context.landmarkEditState.selectedIndex >= 0 &&
        context.landmarkEditState.selectedIndex < static_cast<int>(registration->landmarks.size()) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        registration->landmarks.erase(registration->landmarks.begin() + context.landmarkEditState.selectedIndex);
        context.landmarkEditState.selectedIndex = -1;
        context.landmarkEditState.target = LandmarkEditTarget::None;
        context.landmarkEditState.isDragging = false;
    }

    if (m_content == ViewerContent::StackB && context.pendingLandmarkPoint.hasMovingPoint)
    {
        const ImVec2 pendingPoint = ToScreenPoint(context.pendingLandmarkPoint.movingX,
                                                  context.pendingLandmarkPoint.movingY,
                                                  min,
                                                  max,
                                                  m_texture.GetWidth(),
                                                  m_texture.GetHeight());
        drawList->AddCircle(pendingPoint, 8.0f, IM_COL32(90, 220, 255, 255), 0, 2.0f);
        drawList->AddText(ImVec2(pendingPoint.x + 8.0f, pendingPoint.y - 8.0f), IM_COL32(180, 240, 255, 255),
                          "pending");
    }

    ImGui::SetCursorScreenPos(ImVec2(canvasStart.x, canvasStart.y));
    ImGui::Dummy(canvasSize);
}
} // namespace align
