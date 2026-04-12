#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace align
{
std::optional<std::filesystem::path> ShowOpenFileDialog(const wchar_t* title,
                                                        const wchar_t* filterName,
                                                        const wchar_t* filterPattern);
std::optional<std::filesystem::path> ShowSaveFileDialog(const wchar_t* title,
                                                        const wchar_t* defaultExtension,
                                                        const wchar_t* filterName,
                                                        const wchar_t* filterPattern,
                                                        const std::wstring& defaultFileName);
std::optional<std::filesystem::path> ShowSelectFolderDialog(const wchar_t* title);
} // namespace align
