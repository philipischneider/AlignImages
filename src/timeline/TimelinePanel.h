#pragma once

#include "app/AppContext.h"
#include "io/ImageLoader.h"
#include "viewer/ImageTexture.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace align
{
class TimelinePanel
{
public:
    void Draw(AppContext& context, float height);

private:
    struct ThumbnailEntry
    {
        std::string filePath;
        std::unique_ptr<ImageTexture> texture;
    };

    // Returns the content width used by this row (for shared scrollbar sizing)
    float DrawStackTimeline(AppContext& context,
                            const char* label,
                            StackModel& stack,
                            int& activeIndex,
                            int& timelineOffset,
                            int normalizedOffset);
    ImageTexture* GetOrCreateThumbnail(const SliceRecord& slice);

    ImageLoader m_imageLoader;
    std::unordered_map<std::string, ThumbnailEntry> m_thumbnailCache;
    std::string m_draggingTimelineId;
    float m_dragStartMouseX = 0.0f;
    int m_dragStartOffset = 0;
};
} // namespace align
