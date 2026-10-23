#pragma once

#include "renamer_core.h"

#include <windows.h>
#include <string>
#include <vector>

namespace renamer {

struct OperationResult {
    bool ok = false;
    size_t changed = 0;
    DWORD win32Error = ERROR_SUCCESS;
    fs::path logPath;
    std::wstring message;
};

OperationResult ExecuteRename(const fs::path& root, const std::vector<PlanItem>& plan);
OperationResult UndoLatest(const fs::path& root);
bool WriteUtf8Atomic(const fs::path& path, const std::string& data, std::wstring& error);

} // namespace renamer
