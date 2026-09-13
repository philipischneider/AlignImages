#include "timeline/TimelinePanel.h"

#include "imgui.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace align
{
namespace
{
constexpr float kThumbnailButtonSize = 56.0f;
constexpr float kTimelineCellAdvance = 64.0f;

ImVec4 ColorForPairStatus(PairStatus status)
{
    switch (status)
    {
    case PairStatus::Aligned:
        return ImVec4(0.20f, 0.42f, 0.28f, 1.0f);
    case PairStatus::Suspect:
        return ImVec4(0.46f, 0.28f, 0.15f, 1.0f);
    case PairStatus::Manual:
        return ImVec4(0.35f, 0.27f, 0.48f, 1.0f);
    case PairStatus::Candidate:
        return ImVec4(0.24f, 0.31f, 0.37f, 1.0f);
    case PairStatus::Unmatched:
    default:
        return ImVec4(0.31f, 0.20f, 0.20f, 1.0f);
    }
}

bool TryResolveStackAFromStackB(const AppContext& context, int stackBIndex, int& resolvedAIndex)
{
    for (int i = 0; i < static_cast<int>(GetActivePairing(context.session).pairs.size()); ++i)
    {
        const PairRecord& pair = GetActivePairing(context.session).pairs[i];
        if (pair.valid && pair.movingIndex == stackBIndex)
        {
            resolvedAIndex = pair.fixedIndex;
            return true;
        }
    }

    return false;
}

PairStatus ResolveStackBStatus(const AppContext& context, int stackBIndex)
{
    for (const PairRecord& pair : GetActivePairing(context.session).pairs)
    {
        if (pair.valid && pair.movingIndex == stackBIndex)
        {
            return pair.status;
        }
    }

    return PairStatus::Unmatched;
}

enum class LandmarkMarkerKind
{
    None,
    Propagated,  // isManual but no landmark points (transform copied from another pair)
    Real,        // isManual with actual landmark point pairs
};

LandmarkMarkerKind LandmarkMarkerForStackA(const AppContext& context, int stackAIndex)
{
    if (stackAIndex < 0 || stackAIndex >= static_cast<int>(GetActivePairing(context.session).pairs.size()))
        return LandmarkMarkerKind::None;

    const PairRecord& pair = GetActivePairing(context.session).pairs[stackAIndex];
    if (!pair.valid)
        return LandmarkMarkerKind::None;

    const RegistrationResult* reg =
        FindRegistrationResult(context.session.registrations, GetActivePairing(context.session).fixedStackId, GetActivePairing(context.session).movingStackId, pair.fixedIndex, pair.movingIndex);
    if (reg == nullptr || !reg->isManual)
        return LandmarkMarkerKind::None;

    return reg->landmarks.size() >= 2 ? LandmarkMarkerKind::Real : LandmarkMarkerKind::Propagated;
}

LandmarkMarkerKind LandmarkMarkerForStackB(const AppContext& context, int stackBIndex)
{
    for (const PairRecord& pair : GetActivePairing(context.session).pairs)
    {
        if (!pair.valid || pair.movingIndex != stackBIndex)
            continue;

        const RegistrationResult* reg =
            FindRegistrationResult(context.session.registrations, GetActivePairing(context.session).fixedStackId, GetActivePairing(context.session).movingStackId, pair.fixedIndex, pair.movingIndex);
        if (reg == nullptr || !reg->isManual)
            continue;

        return reg->landmarks.size() >= 2 ? LandmarkMarkerKind::Real : LandmarkMarkerKind::Propagated;
    }

    return LandmarkMarkerKind::None;
}
} // namespace

void TimelinePanel::Draw(AppContext& context, float height)
{
    ImGui::BeginChild("TimelineRegion", ImVec2(0.0f, height), true);

    ImGui::TextUnformatted("Timeline");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("Thumbnail timeline for both stacks.\n"
                          "Click and drag anywhere on a stack's colored block to shift its pairing offset.\n"
                          "Markers below thumbnails:\n"
                          "  amber  = real landmarks placed manually\n"
                          "  violet = transform propagated (no landmark points)");
    }
    ImGui::TextWrapped("Clique e arraste o bloco colorido de cada stack (como um clipe de video) para deslocar a correspondencia.");
    ImGui::Text("Derived Offset (B -> A): %d", GetActivePairing(context.session).globalOffset);
    ImGui::Separator();

    const int normalizedBase = -(std::min)(0, (std::min)(GetActivePairing(context.session).fixedTimelineOffset,
                                                          GetActivePairing(context.session).movingTimelineOffset));
    const int normOffsetA = GetActivePairing(context.session).fixedTimelineOffset  + normalizedBase;
    const int normOffsetB = GetActivePairing(context.session).movingTimelineOffset + normalizedBase;

    // Both stacks share a single scrollable area â€” no more vertical scrolling to find Stack B
    ImGui::BeginChild("Timelines", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    const float widthA = DrawStackTimeline(context,
                                           "Stack A",
                                           GetActiveFixedStack(context.session),
                                           GetActivePairing(context.session).activeFixedIndex,
                                           GetActivePairing(context.session).fixedTimelineOffset,
                                           normOffsetA);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float widthB = DrawStackTimeline(context,
                                           "Stack B",
                                           GetActiveMovingStack(context.session),
                                           GetActivePairing(context.session).activeMovingIndex,
                                           GetActivePairing(context.session).movingTimelineOffset,
                                           normOffsetB);

    // One Dummy sized to the wider row ensures the scrollbar covers both stacks
    ImGui::Dummy(ImVec2((std::max)(widthA, widthB), 1.0f));

    ImGui::EndChild();
    ImGui::EndChild();
}

float TimelinePanel::DrawStackTimeline(AppContext& context,
                                       const char* label,
                                       StackModel& stack,
                                       int& activeIndex,
                                       int& timelineOffset,
                                       int normalizedOffset)
{
    ImGui::Text("%s (%d slices)", label, static_cast<int>(stack.slices.size()));

    ImGui::PushID(label);

    constexpr float kClipLabelBandHeight = 20.0f;
    constexpr float kRowHeight = kClipLabelBandHeight + kThumbnailButtonSize + 34.0f;
    const float clipWidth = static_cast<float>(stack.slices.size()) * kTimelineCellAdvance;
    const float trackWidth = (std::max)(ImGui::GetContentRegionAvail().x,
                                        static_cast<float>(normalizedOffset) * kTimelineCellAdvance + clipWidth + 24.0f);

    const ImVec2 trackOrigin = ImGui::GetCursorScreenPos();

    // The whole row is one drag surface: clicking and dragging anywhere on the clip -- including on
    // top of a thumbnail -- shifts the pairing offset, like dragging a clip in a video editor timeline.
    // A short click without movement still reaches the thumbnail button drawn on top (see below), which
    // handles slice selection independently.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("drag_track", ImVec2(trackWidth, kRowHeight));
    if (ImGui::IsItemActivated())
    {
        m_draggingTimelineId = label;
        m_dragStartMouseX = ImGui::GetIO().MousePos.x;
        m_dragStartOffset = timelineOffset;
    }
    const bool isDraggingThis = (m_draggingTimelineId == label);
    if (isDraggingThis && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float deltaX = ImGui::GetIO().MousePos.x - m_dragStartMouseX;
        const int deltaSlots = static_cast<int>(std::round(deltaX / kTimelineCellAdvance));
        const int nextOffset = m_dragStartOffset + deltaSlots;
        if (nextOffset != timelineOffset)
        {
            timelineOffset = nextOffset;
            RebuildPairs(context.session, GetActivePairing(context.session));
        }
    }
    if (isDraggingThis && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_draggingTimelineId.clear();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool isStackA = (&stack == &GetActiveFixedStack(context.session));
    const ImU32 clipColor = isStackA ? IM_COL32(40, 78, 122, 255) : IM_COL32(140, 84, 28, 255);
    const ImU32 clipLabelBandColor = isStackA ? IM_COL32(30, 60, 96, 255) : IM_COL32(110, 64, 20, 255);

    const float clipStartX = trackOrigin.x + static_cast<float>(normalizedOffset) * kTimelineCellAdvance;
    const ImVec2 clipMin(clipStartX, trackOrigin.y);
    const ImVec2 clipMax(clipStartX + clipWidth, trackOrigin.y + kRowHeight);
    drawList->AddRectFilled(clipMin, clipMax, clipColor, 6.0f);
    drawList->AddRectFilled(clipMin, ImVec2(clipMax.x, clipMin.y + kClipLabelBandHeight), clipLabelBandColor, 6.0f,
                            ImDrawFlags_RoundCornersTop);
    drawList->AddText(ImVec2(clipMin.x + 8.0f, clipMin.y + 2.0f), IM_COL32(235, 240, 245, 255), label);
    if (isDraggingThis)
    {
        drawList->AddRect(clipMin, clipMax, IM_COL32(255, 255, 255, 230), 6.0f, 0, 2.5f);
    }

    const float scrollX = ImGui::GetScrollX();
    const float viewWidth = ImGui::GetWindowWidth();
    const int firstVisibleIndex = (std::max)(0, static_cast<int>(std::floor((scrollX / kTimelineCellAdvance))) - normalizedOffset - 2);
    const int lastVisibleIndex = (std::min)(static_cast<int>(stack.slices.size()) - 1,
                                            static_cast<int>(std::ceil((scrollX + viewWidth) / kTimelineCellAdvance)) - normalizedOffset + 2);

    for (int i = firstVisibleIndex; i <= lastVisibleIndex; ++i)
    {
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(clipStartX + static_cast<float>(i) * kTimelineCellAdvance,
                                         trackOrigin.y + kClipLabelBandHeight + 4.0f));
        const bool isActive = (i == activeIndex);
        PairStatus status = PairStatus::Unmatched;
        if (&stack == &GetActiveFixedStack(context.session))
        {
            if (i < static_cast<int>(GetActivePairing(context.session).pairs.size()))
            {
                status = GetActivePairing(context.session).pairs[i].status;
            }
        }
        else
        {
            status = ResolveStackBStatus(context, i);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ColorForPairStatus(status));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.46f, 0.52f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.46f, 0.79f, 1.0f));

        if (isActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.92f, 0.95f, 0.98f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        }

        bool clicked = false;
        ImageTexture* thumbnail = nullptr;
        if (!context.batchProcessState.running)
        {
            thumbnail = GetOrCreateThumbnail(stack, stack.slices[i]);
        }
        ImGui::BeginGroup();
        if (thumbnail != nullptr && thumbnail->IsValid())
        {
            clicked = ImGui::ImageButton(("thumb_" + std::to_string(i)).c_str(),
                                         reinterpret_cast<void*>(static_cast<intptr_t>(thumbnail->GetId())),
                                         ImVec2(kThumbnailButtonSize, kThumbnailButtonSize));
        }
        else
        {
            clicked = ImGui::Button(("thumb_" + std::to_string(i)).c_str(), ImVec2(kThumbnailButtonSize, kThumbnailButtonSize));
        }

        if (clicked)
        {
            activeIndex = i;
            context.selectedHistoryIndex = -1;
            context.selectedOperationId = 0;
            if (&stack == &GetActiveFixedStack(context.session) && i < static_cast<int>(GetActivePairing(context.session).pairs.size()))
            {
                const PairRecord& pair = GetActivePairing(context.session).pairs[i];
                if (pair.valid)
                {
                    GetActivePairing(context.session).activeMovingIndex = pair.movingIndex;
                }
            }
            else if (&stack == &GetActiveMovingStack(context.session))
            {
                int resolvedAIndex = -1;
                if (TryResolveStackAFromStackB(context, i, resolvedAIndex))
                {
                    GetActivePairing(context.session).activeFixedIndex = resolvedAIndex;
                }
            }
        }

        ImGui::Text("%03d", i);
        const LandmarkMarkerKind markerKind = (&stack == &GetActiveFixedStack(context.session))
                                                  ? LandmarkMarkerForStackA(context, i)
                                                  : LandmarkMarkerForStackB(context, i);
        if (markerKind != LandmarkMarkerKind::None)
        {
            const ImVec2 markerMin = ImGui::GetItemRectMin();
            const ImVec2 markerMax = ImGui::GetItemRectMax();
            const ImU32 color = (markerKind == LandmarkMarkerKind::Real)
                                    ? IM_COL32(255, 182, 66, 255)   // amber  â€” real landmarks
                                    : IM_COL32(160, 100, 220, 255); // violet â€” propagated transform
            drawList->AddRectFilled(ImVec2(markerMin.x + 8.0f, markerMax.y + 2.0f),
                                    ImVec2(markerMin.x + 20.0f, markerMax.y + 10.0f),
                                    color,
                                    2.0f);
        }
        ImGui::EndGroup();

        if (isActive)
        {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(trackOrigin.x, trackOrigin.y + kRowHeight));
    ImGui::PopID();
    return trackWidth;
}

namespace
{
constexpr size_t kMaxConcurrentThumbnailDecodes = 2;

cv::Mat DecodeThumbnail(const std::string& filePath, const ImageLoadOptions& loadOptions)
{
    cv::Mat image;
    ImageLoader loader;
    const Result result = loader.LoadColorImage(filePath, image, loadOptions);
    if (!result.ok || image.empty())
    {
        return cv::Mat{};
    }

    constexpr int kThumbnailSize = 64;
    const double scale = std::min(1.0, static_cast<double>(kThumbnailSize) /
                                           static_cast<double>((std::max)(image.cols, image.rows)));
    cv::Mat thumbnail;
    cv::resize(image, thumbnail, cv::Size(), scale, scale, cv::INTER_AREA);
    return thumbnail;
}

std::string MakeThumbnailCacheKey(const SliceRecord& slice)
{
    return slice.filePath + "|" + (slice.flipHorizontal ? "1" : "0") + (slice.flipVertical ? "1" : "0") +
           std::to_string(slice.rotationDegrees);
}
} // namespace

ImageTexture* TimelinePanel::GetOrCreateThumbnail(const StackModel& stack, const SliceRecord& slice)
{
    if (slice.filePath.empty())
    {
        return nullptr;
    }

    const std::string cacheKey = MakeThumbnailCacheKey(slice);

    auto found = m_thumbnailCache.find(cacheKey);
    if (found != m_thumbnailCache.end())
    {
        return found->second.texture.get();
    }

    auto pending = m_pendingThumbnails.find(cacheKey);
    if (pending != m_pendingThumbnails.end())
    {
        if (pending->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return nullptr; // still decoding in the background
        }

        cv::Mat thumbnail = pending->second.get();
        m_pendingThumbnails.erase(pending);
        if (thumbnail.empty())
        {
            return nullptr;
        }

        ThumbnailEntry entry;
        entry.filePath = slice.filePath;
        entry.texture = std::make_unique<ImageTexture>();
        if (!entry.texture->Upload(thumbnail).ok)
        {
            return nullptr;
        }

        auto [it, inserted] = m_thumbnailCache.emplace(cacheKey, std::move(entry));
        return it->second.texture.get();
    }

    if (m_pendingThumbnails.size() >= kMaxConcurrentThumbnailDecodes)
    {
        return nullptr; // avoid flooding the disk with many concurrent random reads
    }

    const std::string filePath = slice.filePath;
    const ImageLoadOptions loadOptions = MakeDefaultLoadOptions(stack, slice);
    m_pendingThumbnails.emplace(cacheKey,
                                std::async(std::launch::async, DecodeThumbnail, filePath, loadOptions));
    return nullptr;
}
} // namespace align
