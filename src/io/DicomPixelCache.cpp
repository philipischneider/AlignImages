#include "io/DicomPixelCache.h"

#include "io/DicomLoader.h"

namespace align
{
DicomPixelCache::DicomPixelCache(size_t maxEntries)
    : m_maxEntries(maxEntries)
{
}

cv::Mat DicomPixelCache::GetOrDecode(const std::string& filePath)
{
    auto found = m_entries.find(filePath);
    if (found != m_entries.end())
    {
        m_lruOrder.erase(found->second.second);
        m_lruOrder.push_front(filePath);
        found->second.second = m_lruOrder.begin();
        return found->second.first;
    }

    cv::Mat raw;
    DicomLoader loader;
    const Result result = loader.ReadPixelData(filePath, raw);
    if (!result.ok)
    {
        return cv::Mat{};
    }

    if (m_entries.size() >= m_maxEntries && !m_lruOrder.empty())
    {
        const std::string& lruKey = m_lruOrder.back();
        m_entries.erase(lruKey);
        m_lruOrder.pop_back();
    }

    m_lruOrder.push_front(filePath);
    m_entries[filePath] = {raw, m_lruOrder.begin()};
    return raw;
}

void DicomPixelCache::Clear()
{
    m_entries.clear();
    m_lruOrder.clear();
}
} // namespace align
