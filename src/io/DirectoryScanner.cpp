#include "io/DirectoryScanner.h"

#include "io/DicomLoader.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>
#include <vector>

namespace align
{
namespace
{
std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool IsSupportedImageExtension(const std::filesystem::path& path)
{
    static const std::set<std::string> kSupported {
        ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp", ".dcm"
    };

    return kSupported.contains(LowerExtension(path));
}

bool IsDicomExtension(const std::filesystem::path& path)
{
    return LowerExtension(path) == ".dcm";
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

    if (files.empty())
    {
        return Result{false, "No supported image files were found in the selected directory."};
    }

    const int dicomCount = static_cast<int>(std::count_if(files.begin(), files.end(), IsDicomExtension));
    if (dicomCount > 0 && dicomCount != static_cast<int>(files.size()))
    {
        return Result{false, "The selected directory mixes DICOM (.dcm) files with other image formats. Please keep DICOM series in their own directory."};
    }

    StackModel loaded;
    loaded.id = stackId;
    loaded.name = stackName;
    loaded.modality = modality;
    loaded.directory = directory.string();
    std::string resultMessage;

    if (dicomCount > 0)
    {
        DicomLoader dicomLoader;
        struct DicomFileEntry
        {
            std::filesystem::path path;
            DicomMetadata metadata;
        };

        std::vector<DicomFileEntry> entries;
        entries.reserve(files.size());
        for (const std::filesystem::path& file : files)
        {
            DicomFileEntry entry;
            entry.path = file;
            const Result headerResult = dicomLoader.ReadHeaderMetadata(file, entry.metadata);
            if (!headerResult.ok)
            {
                return Result{false, "Could not read DICOM metadata from " + file.filename().string() + ": " + headerResult.message};
            }
            entries.push_back(std::move(entry));
        }

        const bool allHaveInstanceNumber = std::all_of(entries.begin(), entries.end(), [](const DicomFileEntry& entry)
        {
            return entry.metadata.instanceNumber >= 0;
        });

        std::string orderingStrategy;
        if (allHaveInstanceNumber)
        {
            std::sort(entries.begin(), entries.end(), [](const DicomFileEntry& a, const DicomFileEntry& b)
            {
                return a.metadata.instanceNumber < b.metadata.instanceNumber;
            });
            orderingStrategy = "DICOM InstanceNumber (0020,0013)";
        }
        else
        {
            const bool allHaveSliceLocation = std::all_of(entries.begin(), entries.end(), [](const DicomFileEntry& entry)
            {
                return entry.metadata.sliceLocation != 0.0;
            });
            if (allHaveSliceLocation)
            {
                std::sort(entries.begin(), entries.end(), [](const DicomFileEntry& a, const DicomFileEntry& b)
                {
                    return a.metadata.sliceLocation < b.metadata.sliceLocation;
                });
                orderingStrategy = "DICOM SliceLocation (0020,1041) -- some files were missing InstanceNumber";
            }
            else
            {
                std::sort(entries.begin(), entries.end(), [](const DicomFileEntry& a, const DicomFileEntry& b)
                {
                    return a.path < b.path;
                });
                orderingStrategy = "file name (WARNING: neither InstanceNumber nor SliceLocation were available on all files -- ordering may not reflect anatomical order)";
            }
        }

        loaded.isDicom = true;
        const DicomMetadata& firstMetadata = entries.front().metadata;
        loaded.rescaleSlope = firstMetadata.rescaleSlope;
        loaded.rescaleIntercept = firstMetadata.rescaleIntercept;

        if (firstMetadata.hasWindowTag)
        {
            loaded.defaultWindowCenter = firstMetadata.windowCenter;
            loaded.defaultWindowWidth = firstMetadata.windowWidth;
        }
        else
        {
            cv::Mat firstRaw;
            if (dicomLoader.ReadPixelData(entries.front().path, firstRaw).ok && !firstRaw.empty() && firstRaw.channels() == 1)
            {
                double minVal = 0.0;
                double maxVal = 0.0;
                cv::minMaxLoc(firstRaw, &minVal, &maxVal);
                const double minHu = minVal * loaded.rescaleSlope + loaded.rescaleIntercept;
                const double maxHu = maxVal * loaded.rescaleSlope + loaded.rescaleIntercept;
                loaded.defaultWindowCenter = (minHu + maxHu) / 2.0;
                loaded.defaultWindowWidth = (std::max)(maxHu - minHu, 1.0);
            }
            else
            {
                loaded.defaultWindowCenter = 128.0;
                loaded.defaultWindowWidth = 256.0;
            }
        }
        loaded.windowCenter = loaded.defaultWindowCenter;
        loaded.windowWidth = loaded.defaultWindowWidth;

        for (int index = 0; index < static_cast<int>(entries.size()); ++index)
        {
            const DicomFileEntry& entry = entries[static_cast<size_t>(index)];
            SliceRecord slice;
            slice.stackIndex = index;
            slice.filePath = entry.path.string();
            slice.fileName = entry.path.filename().string();
            slice.width = entry.metadata.width;
            slice.height = entry.metadata.height;
            slice.instanceNumber = entry.metadata.instanceNumber;
            slice.sliceLocation = entry.metadata.sliceLocation;
            loaded.slices.push_back(slice);
        }
        resultMessage = "DICOM series ordered by " + orderingStrategy + ".";
    }
    else
    {
        std::sort(files.begin(), files.end());
        for (int index = 0; index < static_cast<int>(files.size()); ++index)
        {
            SliceRecord slice;
            slice.stackIndex = index;
            slice.filePath = files[index].string();
            slice.fileName = files[index].filename().string();
            loaded.slices.push_back(slice);
        }
    }

    stack = std::move(loaded);
    return Result{true, resultMessage};
}
} // namespace align
