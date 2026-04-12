#pragma once

#include "app/AppContext.h"
#include "export/ExportController.h"
#include "io/DirectoryScanner.h"
#include "io/ImageLoader.h"
#include "io/SessionSerializer.h"
#include "registration/ConvergenceAnalyzer.h"
#include "registration/LandmarkRegistration.h"
#include "registration/RegistrationEngine.h"
#include "timeline/TimelinePanel.h"
#include "viewer/ViewerPanel.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

struct GLFWwindow;

namespace align
{
class MainWindow
{
public:
    void Draw(AppContext& context, GLFWwindow* window);
    struct BatchSummary
    {
        int attempted = 0;
        int succeeded = 0;
        int failed = 0;
        double bestScore = 0.0;
        double worstScore = 0.0;
        double averageScore = 0.0;
        bool hasScores = false;
    };

    struct StackLoadTaskResult
    {
        Result result;
        StackModel stack;
        std::string stackId;
    };

    struct ConvergenceTaskResult
    {
        Result result;
        std::vector<RegistrationResult> registrations;
        int outlierCount = 0;
    };

    struct BatchTaskProgress
    {
        std::atomic<bool> cancelRequested {false};
        std::atomic<int> attempted {0};
        std::atomic<int> succeeded {0};
        std::atomic<int> failed {0};
        std::atomic<int> totalValidPairs {0};
        std::atomic<int> currentFixedIndex {-1};
        std::atomic<int> currentMovingIndex {-1};
        std::mutex statusMutex;
        std::string statusMessage;
    };

    struct BatchTaskResult
    {
        Result result;
        std::vector<PairRecord> pairs;
        std::vector<RegistrationResult> registrations;
        int attempted = 0;
        int succeeded = 0;
        int failed = 0;
        bool cancelled = false;
    };

private:

    void DrawMenuBar(AppContext& context);
    void DrawLeftPanel(AppContext& context, GLFWwindow* window);
    void DrawRightPanel(AppContext& context);
    void DrawCenterPanel(AppContext& context);
    void ProcessBatchStep(AppContext& context);
    void ProcessAsyncTasks(AppContext& context);
    void DrawStackLoader(AppContext& context, StackModel& stack);
    void LoadStack(AppContext& context, StackModel& stack);
    void RunCurrentAlignment(AppContext& context);
    void RunBatchAlignment(AppContext& context);
    void CancelBatchAlignment(AppContext& context);
    void LoadSession(AppContext& context);
    void ExportCurrentAligned(AppContext& context);
    void ExportBatchAligned(AppContext& context);
    void AnalyzeConvergence(AppContext& context);
    void RunPriorRefinement(AppContext& context);
    void ApplyManualLandmarks(AppContext& context);
    void DrawLandmarkEditor(AppContext& context);
    RegistrationResult* GetOrCreateCurrentRegistration(AppContext& context);
    void StoreRegistrationResult(AppContext& context, const RegistrationResult& computed);
    BatchSummary BuildBatchSummary(const AppContext& context) const;
    bool HasBackgroundTask() const;

    ViewerPanel m_referenceViewer {"Reference Viewer", ViewerContent::StackA};
    ViewerPanel m_movingViewer {"Moving Viewer", ViewerContent::StackB};
    ViewerPanel m_previewViewer {"Preview Viewer", ViewerContent::Preview};
    TimelinePanel m_timelinePanel;
    SessionSerializer m_serializer;
    DirectoryScanner m_directoryScanner;
    ImageLoader m_imageLoader;
    ConvergenceAnalyzer m_convergenceAnalyzer;
    RegistrationEngine m_registrationEngine;
    LandmarkRegistration m_landmarkRegistration;
    ExportController m_exportController;
    std::optional<std::future<StackLoadTaskResult>> m_stackLoadTask;
    std::optional<std::future<ConvergenceTaskResult>> m_convergenceTask;
    std::optional<std::future<BatchTaskResult>> m_batchTask;
    std::shared_ptr<BatchTaskProgress> m_batchTaskProgress;
    std::string m_backgroundStatus;
    std::string m_lastMessage;
};
} // namespace align
