#pragma once

#include "data/SessionModel.h"
#include "io/DicomPixelCache.h"

namespace align
{
struct PendingLandmarkPoint
{
    bool hasMovingPoint = false;
    double movingX = 0.0;
    double movingY = 0.0;
};

enum class LandmarkEditTarget
{
    None,
    Moving,
    Fixed
};

struct LandmarkEditState
{
    int selectedIndex = -1;
    bool isDragging = false;
    LandmarkEditTarget target = LandmarkEditTarget::None;
};

struct BatchProcessState
{
    bool running = false;
    bool cancelRequested = false;
    int nextPairIndex = 0;
    int attempted = 0;
    int succeeded = 0;
    int failed = 0;
    int totalValidPairs = 0;
    int currentFixedIndex = -1;
    int currentMovingIndex = -1;
    std::string statusMessage;
};

struct AppContext
{
    SessionModel session;
    bool showDemoWindow = false;
    bool landmarkModeEnabled = false;
    int selectedHistoryIndex = -1;
    int selectedOperationId = 0;
    PendingLandmarkPoint pendingLandmarkPoint;
    LandmarkEditState landmarkEditState;
    BatchProcessState batchProcessState;
    DicomPixelCache dicomPixelCache;
};
} // namespace align
