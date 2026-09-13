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

// Which handle of the Transpose gizmo is currently being dragged (or none). The pivot handle
// moves the whole image; the control handle combines rotation and scale into a single drag (its
// distance from the pivot drives scale, its angle relative to the pivot drives rotation).
enum class TransposeZone
{
    None,
    Move,
    RotateScale
};

// A ZBrush-Transpose-style action line for fine manual refinement of the active pair, drawn in
// the Preview viewer: first drag places the pivot+control point, then dragging one of the 3
// handles on the persisted line moves/rotates/scales the moving image around the pivot.
struct TransposeState
{
    bool hasActionLine = false;
    double pivotX = 0.0;
    double pivotY = 0.0;
    double controlX = 0.0;
    double controlY = 0.0;
    bool isPlacingActionLine = false;
    TransposeZone activeDragZone = TransposeZone::None;
    // Snapshot of pivot/control taken when a drag starts, so deltas are measured from a stable
    // reference rather than drifting frame-to-frame.
    double dragStartPivotX = 0.0;
    double dragStartPivotY = 0.0;
    double dragStartControlX = 0.0;
    double dragStartControlY = 0.0;
    double dragStartMouseImageX = 0.0;
    double dragStartMouseImageY = 0.0;
    Transform2D dragStartAdjustment; // snapshot of manualAdjustment taken when the current drag began
};

struct AppContext
{
    SessionModel session;
    float uiScale = 1.0f; // resolved DPI/UI scale for the current frame; used to scale hand-drawn UI elements
                          // (crosshairs, hit-test radii, markers) that ImGuiStyle::ScaleAllSizes doesn't reach.
    float sharedZoom = 1.0f; // used by all 3 viewers instead of their own zoom/pan when Sync Viewports is on
    float sharedPanX = 0.0f;
    float sharedPanY = 0.0f;
    bool showDemoWindow = false;
    bool landmarkModeEnabled = false;
    bool transposeModeEnabled = false;
    int selectedHistoryIndex = -1;
    int selectedOperationId = 0;
    PendingLandmarkPoint pendingLandmarkPoint;
    LandmarkEditState landmarkEditState;
    TransposeState transposeState;
    BatchProcessState batchProcessState;
    DicomPixelCache dicomPixelCache;
};
} // namespace align
