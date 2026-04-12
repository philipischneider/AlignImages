#include "core/WinDialogUtils.h"

#include <windows.h>
#include <shobjidl.h>

namespace align
{
namespace
{
std::optional<std::filesystem::path> ShowFileDialog(const wchar_t* title,
                                                    DWORD options,
                                                    bool saveDialog,
                                                    const COMDLG_FILTERSPEC* filters,
                                                    UINT filterCount,
                                                    const wchar_t* defaultExtension,
                                                    const std::wstring* defaultFileName)
{
    IFileDialog* dialog = nullptr;
    HRESULT hr = saveDialog ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&dialog))
                            : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr)
    {
        return std::nullopt;
    }

    DWORD currentOptions = 0;
    if (SUCCEEDED(dialog->GetOptions(&currentOptions)))
    {
        dialog->SetOptions(currentOptions | options);
    }

    if (title != nullptr)
    {
        dialog->SetTitle(title);
    }

    if (filters != nullptr && filterCount > 0)
    {
        dialog->SetFileTypes(filterCount, filters);
    }

    if (defaultExtension != nullptr)
    {
        dialog->SetDefaultExtension(defaultExtension);
    }

    if (defaultFileName != nullptr && !defaultFileName->empty())
    {
        dialog->SetFileName(defaultFileName->c_str());
    }

    std::optional<std::filesystem::path> result;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
        {
            PWSTR rawPath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) && rawPath != nullptr)
            {
                result = std::filesystem::path(rawPath);
                CoTaskMemFree(rawPath);
            }
            item->Release();
        }
    }

    dialog->Release();
    return result;
}
} // namespace

std::optional<std::filesystem::path> ShowOpenFileDialog(const wchar_t* title,
                                                        const wchar_t* filterName,
                                                        const wchar_t* filterPattern)
{
    const COMDLG_FILTERSPEC filter[] = {
        {filterName, filterPattern}
    };
    return ShowFileDialog(title, FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST, false, filter, 1, nullptr,
                          nullptr);
}

std::optional<std::filesystem::path> ShowSaveFileDialog(const wchar_t* title,
                                                        const wchar_t* defaultExtension,
                                                        const wchar_t* filterName,
                                                        const wchar_t* filterPattern,
                                                        const std::wstring& defaultFileName)
{
    const COMDLG_FILTERSPEC filter[] = {
        {filterName, filterPattern}
    };
    return ShowFileDialog(title, FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT, true, filter, 1,
                          defaultExtension, &defaultFileName);
}

std::optional<std::filesystem::path> ShowSelectFolderDialog(const wchar_t* title)
{
    return ShowFileDialog(title, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST, false, nullptr, 0, nullptr,
                          nullptr);
}
} // namespace align
