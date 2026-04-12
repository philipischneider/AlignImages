#include "io/DirectoryScanner.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>
#include <vector>

namespace align
{
namespace
{
bool IsSupportedImageExtension(const std::filesystem::path& path)
{
    static const std::set<std::string> kSupported {
        ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp"
    };

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return kSupported.contains(extension);
}
} // namespace

Result DirectoryScanner::LoadStackFromDirectory(const std::filesystem::path& directory,
                                                const std::string& stackId,
                                                const std::string& stackName,
                                                const std::string& modality,
                                                StackModel& stack) const
{
    if (!std::filesystem::exists(directory))
    {
        return Result{false, "Directory does not exist."};
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        if (IsSupportedImageExtension(entry.path()))
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    if (files.empty())
    {
        return Result{false, "No supported image files were found in the selected directory."};
    }

    StackModel loaded;
    loaded.id = stackId;
    loaded.name = stackName;
    loaded.modality = modality;
    loaded.directory = directory.string();

    for (int index = 0; index < static_cast<int>(files.size()); ++index)
    {
        SliceRecord slice;
        slice.stackIndex = index;
        slice.filePath = files[index].string();
        slice.fileName = files[index].filename().string();
        loaded.slices.push_back(slice);
    }

    stack = std::move(loaded);
    return Result{};
}
} // namespace align
