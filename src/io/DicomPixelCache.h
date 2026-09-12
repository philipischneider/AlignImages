#pragma once

#include <opencv2/core.hpp>

#include <list>
#include <string>
#include <unordered_map>

namespace align
{
// LRU cache of decoded (native bit depth, pre-window) DICOM pixel buffers, keyed by file
// path. Avoids re-invoking GDCM decode on every Window/Level slider change. Files never
// change on disk during a session, so entries only need eviction, not invalidation.
class DicomPixelCache
{
public:
    explicit DicomPixelCache(size_t maxEntries = 8);

    // Returns the cached raw Mat for filePath, decoding via DicomLoader::ReadPixelData
    // and inserting into the cache on a miss. Returns an empty Mat if decoding fails.
    cv::Mat GetOrDecode(const std::string& filePath);

    void Clear();

private:
    size_t m_maxEntries;
    std::list<std::string> m_lruOrder;
    std::unordered_map<std::string, std::pair<cv::Mat, std::list<std::string>::iterator>> m_entries;
};
} // namespace align
