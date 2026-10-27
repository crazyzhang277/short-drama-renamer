#pragma once

#include "renamer_core.h"

#include <atomic>
#include <functional>

namespace renamer {

struct ArchiveScanItem {
    fs::path source;
    std::vector<long> episodes;
    std::wstring error;
};

bool IsArchiveFile(const fs::path& path);
std::wstring ArchiveSuffix(const fs::path& path);
bool ScanArchiveDirectory(const fs::path& root, bool recursive,
                          std::vector<ArchiveScanItem>& items,
                          const std::atomic<bool>& cancel,
                          const std::function<void(size_t, size_t)>& progress,
                          std::wstring& error);

} // namespace renamer
