#include "io/DicomSeriesStitcher.h"

#include "io/DicomLoader.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cctype>

namespace align
{
namespace
{
bool IsDicomFile(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".dcm";
}

struct StitchEntry
{
    std::filesystem::path path;
    DicomMetadata metadata;
    int seriesIndex = 0; // which input directory this file came from
};
} // namespace

Result DicomSeriesStitcher::LoadStitchedStack(const std::vector<std::filesystem::path>& directories,
                                              const std::string& stackId,
                                              const std::string& stackName,
                                              const std::string& modality,
                                              StackModel& stack) const
{
    if (directories.size() < 2)
    {
        return Result{false, "Select at least 2 directories to stitch into one stack."};
    }

    DicomLoader dicomLoader;
    std::vector<StitchEntry> entries;
    std::vector<std::string> seriesPatientPositions;

    for (int seriesIndex = 0; seriesIndex < static_cast<int>(directories.size()); ++seriesIndex)
    {
        const std::filesystem::path& directory = directories[static_cast<size_t>(seriesIndex)];
        if (!std::filesystem::exists(directory))
        {
            return Result{false, "Directory does not exist: " + directory.string()};
        }

        std::vector<std::filesystem::path> files;
        for (const auto& fsEntry : std::filesystem::directory_iterator(directory))
        {
            if (fsEntry.is_regular_file() && IsDicomFile(fsEntry.path()))
            {
                files.push_back(fsEntry.path());
            }
        }
        if (files.empty())
        {
            return Result{false, "No .dcm files found in: " + directory.string()};
        }

        std::string patientPosition;
        for (const std::filesystem::path& file : files)
        {
            StitchEntry entry;
            entry.path = file;
            entry.seriesIndex = seriesIndex;
            const Result headerResult = dicomLoader.ReadHeaderMetadata(file, entry.metadata);
            if (!headerResult.ok)
            {
                return Result{false, "Could not read DICOM metadata from " + file.filename().string() + ": " + headerResult.message};
            }
            if (patientPosition.empty())
            {
                patientPosition = entry.metadata.patientPosition;
            }
            entries.push_back(std::move(entry));
        }
        seriesPatientPositions.push_back(patientPosition);
    }

    const bool allHavePositionTags = std::all_of(entries.begin(), entries.end(), [](const StitchEntry& entry)
    {
        return entry.metadata.hasPositionTags;
    });

    std::string orderingStrategy;
    if (allHavePositionTags)
    {
        std::sort(entries.begin(), entries.end(), [](const StitchEntry& a, const StitchEntry& b)
        {
            return ComputeProjectedPosition(a.metadata) < ComputeProjectedPosition(b.metadata);
        });
        orderingStrategy = "spatial position (ImagePositionPatient/ImageOrientationPatient), merged across all input series";
    }
    else
    {
        // Fall back to concatenating series in the order they were given, InstanceNumber within each.
        std::stable_sort(entries.begin(), entries.end(), [](const StitchEntry& a, const StitchEntry& b)
        {
            if (a.seriesIndex != b.seriesIndex)
            {
                return a.seriesIndex < b.seriesIndex;
            }
            return a.metadata.instanceNumber < b.metadata.instanceNumber;
        });
        orderingStrategy = "series order + InstanceNumber (WARNING: some files lacked ImagePositionPatient/"
                           "ImageOrientationPatient, so spatial ordering across series could not be verified)";
    }

    const std::string& referencePatientPosition = seriesPatientPositions.front();

    StackModel loaded;
    loaded.id = stackId;
    loaded.name = stackName;
    loaded.modality = modality;
    loaded.directory = directories.front().string();
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

    int mismatchedSeriesCount = 0;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index)
    {
        const StitchEntry& entry = entries[static_cast<size_t>(index)];
        SliceRecord slice;
        slice.stackIndex = index;
        slice.filePath = entry.path.string();
        slice.fileName = entry.path.filename().string();
        slice.width = entry.metadata.width;
        slice.height = entry.metadata.height;
        slice.instanceNumber = entry.metadata.instanceNumber;
        slice.sliceLocation = entry.metadata.sliceLocation;

        // Auto-detect an orientation mismatch between acquisitions (e.g. HFS vs FFS): a
        // patient scanned from the opposite end typically needs a 180-degree in-plane
        // correction relative to the reference series. Editable afterward via the Image
        // Orientation panel if the heuristic guesses wrong for a given series.
        const std::string& thisSeriesPosition = seriesPatientPositions[static_cast<size_t>(entry.seriesIndex)];
        if (!thisSeriesPosition.empty() && !referencePatientPosition.empty() &&
            thisSeriesPosition != referencePatientPosition)
        {
            slice.rotationDegrees = 180;
            if (index == 0 || entries[static_cast<size_t>(index - 1)].seriesIndex != entry.seriesIndex)
            {
                ++mismatchedSeriesCount;
            }
        }

        loaded.slices.push_back(slice);
    }

    stack = std::move(loaded);

    std::string message = "Stitched " + std::to_string(directories.size()) + " series (" +
                          std::to_string(stack.slices.size()) + " slices) ordered by " + orderingStrategy + ".";
    if (mismatchedSeriesCount > 0)
    {
        message += " " + std::to_string(mismatchedSeriesCount) +
                  " series had a different PatientPosition than the first and were auto-rotated 180 degrees -- "
                  "verify with the Image Orientation panel.";
    }
    return Result{true, message};
}
} // namespace align
