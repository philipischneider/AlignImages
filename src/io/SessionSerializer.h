#pragma once

#include "core/Result.h"
#include "data/SessionModel.h"

#include <filesystem>

namespace align
{
class SessionSerializer
{
public:
    Result Save(const SessionModel& session, const std::filesystem::path& filePath) const;
    Result Load(const std::filesystem::path& filePath, SessionModel& session) const;
};
} // namespace align
