#pragma once

#include "app/AppContext.h"
#include "io/ImageLoader.h"
#include "viewer/ImageTexture.h"

#include <string>

struct ImVec2;

namespace align
{
enum class ViewerContent
{
    StackA,
    StackB,
    Preview
};

class ViewerPanel
{
public:
    ViewerPanel(std::string title, ViewerContent content);

    void Draw(AppContext& context, const ImVec2& size);

private:
    void RefreshTexture(AppContext& context);
    void DrawImageCanvas(AppContext& context, const ImVec2& canvasSize);
    void ResetView();
    Result LoadDisplayImage(AppContext& context, const StackModel& stack, const SliceRecord& slice, cv::Mat& outImage);

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
    ImageLoader m_imageLoader;
    ImageTexture m_texture;
    std::string m_statusText = "No image loaded.";
};
} // namespace align
