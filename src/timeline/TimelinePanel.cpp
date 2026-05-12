#include "timeline/TimelinePanel.h"

#include "imgui.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
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
    for (int i = 0; i < static_cast<int>(context.session.pairing.pairs.size()); ++i)
    {
        const PairRecord& pair = context.session.pairing.pairs[i];
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
    for (const PairRecord& pair : context.session.pairing.pairs)
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
    if (stackAIndex < 0 || stackAIndex >= static_cast<int>(context.session.pairing.pairs.size()))
        return LandmarkMarkerKind::None;

    const PairRecord& pair = context.session.pairing.pairs[stackAIndex];
    if (!pair.valid)
        return LandmarkMarkerKind::None;

    const RegistrationResult* reg =
        FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
    if (reg == nullptr || !reg->isManual)
        return LandmarkMarkerKind::None;

    return reg->landmarks.size() >= 2 ? LandmarkMarkerKind::Real : LandmarkMarkerKind::Propagated;
}

LandmarkMarkerKind LandmarkMarkerForStackB(const AppContext& context, int stackBIndex)
{
    for (const PairRecord& pair : context.session.pairing.pairs)
    {
        if (!pair.valid || pair.movingIndex != stackBIndex)
            continue;

        const RegistrationResult* reg =
            FindRegistrationResult(context.session.registrations, pair.fixedIndex, pair.movingIndex);
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
                          "Drag the strip to adjust pairing offsets.\n"
                          "Markers below thumbnails:\n"
                          "  amber  = real landmarks placed manually\n"
                          "  violet = transform propagated (no landmark points)");
    }
    ImGui::TextWrapped("Arraste a faixa de cada timeline para deslocar visualmente os stacks e ajustar a correspondencia.");
    ImGui::Text("Derived Offset (B -> A): %d", context.session.pairing.globalOffset);
    ImGui::Separator();

    const int normalizedBase = -(std::min)(0, (std::min)(context.session.pairing.fixedTimelineOffset,
                                                          context.session.pairing.movingTimelineOffset));
    const int normOffsetA = context.session.pairing.fixedTimelineOffset  + normalizedBase;
    const int normOffsetB = context.session.pairing.movingTimelineOffset + normalizedBase;

    // Both stacks share a single scrollable area — no more vertical scrolling to find Stack B
    ImGui::BeginChild("Timelines", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    const float widthA = DrawStackTimeline(context,
                                           "Stack A",
                                           context.session.stackA,
                                           context.session.projectPreferences.activeSliceA,
                                           context.session.pairing.fixedTimelineOffset,
                                           normOffsetA);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float widthB = DrawStackTimeline(context,
                                           "Stack B",
                                           context.session.stackB,
                                           context.session.projectPreferences.activeSliceB,
                                           context.session.pairing.movingTimelineOffset,
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
    ImGui::InvisibleButton("drag_strip", ImVec2((std::max)(ImGui::GetContentRegionAvail().x, 180.0f), 18.0f));
    if (ImGui::IsItemActivated())
    {
        m_draggingTimelineId = label;
        m_dragStartMouseX = ImGui::GetIO().MousePos.x;
        m_dragStartOffset = timelineOffset;
    }
    if (m_draggingTimelineId == label && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float deltaX = ImGui::GetIO().MousePos.x - m_dragStartMouseX;
        const int deltaSlots = static_cast<int>(std::round(deltaX / kTimelineCellAdvance));
        const int nextOffset = m_dragStartOffset + deltaSlots;
        if (nextOffset != timelineOffset)
        {
            timelineOffset = nextOffset;
            RebuildPairs(context.session);
        }
    }
    if (m_draggingTimelineId == label && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_draggingTimelineId.clear();
    }

    const ImVec2 stripMin = ImGui::GetItemRectMin();
    const ImVec2 stripMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(stripMin, stripMax, IM_COL32(55, 61, 72, 255), 4.0f);
    drawList->AddText(ImVec2(stripMin.x + 8.0f, stripMin.y + 2.0f),
                      IM_COL32(220, 226, 235, 255),
                      "Drag this strip to move the timeline");

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + normalizedOffset * kTimelineCellAdvance);
    const float scrollX = ImGui::GetScrollX();
    const float viewWidth = ImGui::GetWindowWidth();
    const int firstVisibleIndex = (std::max)(0, static_cast<int>(std::floor((scrollX / kTimelineCellAdvance))) - normalizedOffset - 2);
    const int lastVisibleIndex = (std::min)(static_cast<int>(stack.slices.size()) - 1,
                                            static_cast<int>(std::ceil((scrollX + viewWidth) / kTimelineCellAdvance)) - normalizedOffset + 2);

    for (int i = 0; i < static_cast<int>(stack.slices.size()); ++i)
    {
        ImGui::PushID(i);
        const bool isActive = (i == activeIndex);
        PairStatus status = PairStatus::Unmatched;
        if (&stack == &context.session.stackA)
        {
            if (i < static_cast<int>(context.session.pairing.pairs.size()))
            {
                status = context.session.pairing.pairs[i].status;
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
        if (!context.batchProcessState.running && i >= firstVisibleIndex && i <= lastVisibleIndex)
        {
            thumbnail = GetOrCreateThumbnail(stack.slices[i]);
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
            if (&stack == &context.session.stackA && i < static_cast<int>(context.session.pairing.pairs.size()))
            {
                const PairRecord& pair = context.session.pairing.pairs[i];
                if (pair.valid)
                {
                    context.session.projectPreferences.activeSliceB = pair.movingIndex;
                }
            }
            else if (&stack == &context.session.stackB)
            {
                int resolvedAIndex = -1;
                if (TryResolveStackAFromStackB(context, i, resolvedAIndex))
                {
                    context.session.projectPreferences.activeSliceA = resolvedAIndex;
                }
            }
        }

        ImGui::Text("%03d", i);
        const LandmarkMarkerKind markerKind = (&stack == &context.session.stackA)
                                                  ? LandmarkMarkerForStackA(context, i)
                                                  : LandmarkMarkerForStackB(context, i);
        if (markerKind != LandmarkMarkerKind::None)
        {
            const ImVec2 markerMin = ImGui::GetItemRectMin();
            const ImVec2 markerMax = ImGui::GetItemRectMax();
            const ImU32 color = (markerKind == LandmarkMarkerKind::Real)
                                    ? IM_COL32(255, 182, 66, 255)   // amber  — real landmarks
                                    : IM_COL32(160, 100, 220, 255); // violet — propagated transform
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

        if (i + 1 < static_cast<int>(stack.slices.size()))
        {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    const float contentWidth = normalizedOffset * kTimelineCellAdvance +
                               static_cast<float>(stack.slices.size()) * kTimelineCellAdvance + 24.0f;
    ImGui::PopID();
    return contentWidth;
}

ImageTexture* TimelinePanel::GetOrCreateThumbnail(const SliceRecord& slice)
{
    if (slice.filePath.empty())
    {
        return nullptr;
    }

    auto found = m_thumbnailCache.find(slice.filePath);
    if (found != m_thumbnailCache.end())
    {
        return found->second.texture.get();
    }

    cv::Mat image;
    const Result result = m_imageLoader.LoadColorImage(slice.filePath, image);
    if (!result.ok || image.empty())
    {
        return nullptr;
    }

    constexpr int kThumbnailSize = 64;
    const double scale = std::min(1.0, static_cast<double>(kThumbnailSize) /
                                           static_cast<double>((std::max)(image.cols, image.rows)));
    cv::Mat thumbnail;
    cv::resize(image, thumbnail, cv::Size(), scale, scale, cv::INTER_AREA);

    ThumbnailEntry entry;
    entry.filePath = slice.filePath;
    entry.texture = std::make_unique<ImageTexture>();
    if (!entry.texture->Upload(thumbnail).ok)
    {
        return nullptr;
    }

    auto [it, inserted] = m_thumbnailCache.emplace(slice.filePath, std::move(entry));
    return it->second.texture.get();
}
} // namespace align
