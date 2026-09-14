#pragma once

#include "app/AppContext.h"
#include "io/ImageLoader.h"
#include "registration/RegistrationEngine.h"
#include "viewer/ImageTexture.h"

#include <opencv2/core.hpp>

#include <string>

struct ImVec2;
struct ImDrawList;

namespace align
{
enum class ViewerContent
{
    StackA,
    StackB,
    Preview
};

// A resolved zoom/pan target for the current frame: either this viewer's own private state, or
// the AppContext's shared state when Sync Viewports is enabled -- so all three viewers zoom/pan
// together.
struct ZoomPanState
{
    float& zoom;
    float& panX;
    float& panY;
};

class ViewerPanel
{
public:
    ViewerPanel(std::string title, ViewerContent content);

    void Draw(AppContext& context, const ImVec2& size);

private:
    void RefreshTexture(AppContext& context);
    void DrawImageCanvas(AppContext& context, const ImVec2& canvasSize);
    void DrawPreviewModeControls(AppContext& context);
    void DrawTransposeGizmo(AppContext& context, ImDrawList* drawList, const ImVec2& imageMin, const ImVec2& imageMax, bool canvasHovered);
    void ResetView(AppContext& context);
    ZoomPanState GetZoomPanState(AppContext& context);
    Result LoadDisplayImage(AppContext& context, const StackModel& stack, const SliceRecord& slice, cv::Mat& outImage);
    // Returns the decoded (pre-transform) image for `slice`, reusing the last decode when the
    // file/window/orientation are unchanged. Lets the Preview panel re-warp/re-composite on every
    // transform change (dragging landmarks or the Transpose gizmo) without re-hitting disk each frame.
    Result GetOrDecodeImage(AppContext& context, const StackModel& stack, const SliceRecord& slice,
                            std::string& cacheKey, cv::Mat& cacheImage, cv::Mat& outImage);
    // Image to feed alignment-quality metrics (e.g. live Mutual Information): for DICOM stacks,
    // remapped using the stack's fixed default Window/Level rather than the live interactive
    // slider, so the metric reflects alignment quality only -- not whatever the user happens to
    // be looking at right now. Non-DICOM images have no window dependency, so `displayImage` is
    // reused as-is. Reuses the already-decoded raw DICOM buffer (context.dicomPixelCache), no
    // extra disk I/O.
    cv::Mat GetMetricsImage(AppContext& context, const StackModel& stack, const SliceRecord& slice, const cv::Mat& displayImage);

    std::string m_title;
    ViewerContent m_content;
    float m_zoom = 1.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    int m_loadedSliceA = -1;
    int m_loadedSliceB = -1;
    std::string m_loadedFilePath;
    PreviewMode m_loadedPreviewMode = PreviewMode::Blend;
    float m_loadedBlendAlpha = -1.0f;
    int m_loadedCheckerSize = -1;
    double m_loadedRegistrationScore = -1.0;
    bool m_loadedUsedAlignment = false;
    double m_loadedTx = 0.0;
    double m_loadedTy = 0.0;
    double m_loadedTheta = 0.0;
    double m_loadedScale = 1.0;
    double m_loadedWindowCenterA = 0.0;
    double m_loadedWindowWidthA = 0.0;
    double m_loadedWindowCenterB = 0.0;
    double m_loadedWindowWidthB = 0.0;
    bool m_loadedFlipHA = false;
    bool m_loadedFlipVA = false;
    int m_loadedRotationA = -1;
    bool m_loadedFlipHB = false;
    bool m_loadedFlipVB = false;
    int m_loadedRotationB = -1;
    float m_lastFitScale = 1.0f;
    std::string m_decodedCacheKeyA;
    cv::Mat m_decodedImageA;
    std::string m_decodedCacheKeyB;
    cv::Mat m_decodedImageB;
    double m_liveMutualInformation = -1.0; // -1 = not available (e.g. alignment preview is off)
    ImageLoader m_imageLoader;
    RegistrationEngine m_registrationEngine;
    ImageTexture m_texture;
    std::string m_statusText = "No image loaded.";
};
} // namespace align
