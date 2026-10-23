#pragma once

#include "renamer_core.h"

#include <atomic>
#include <functional>

namespace renamer {

struct DramaScan {
    int count = 0;
    long total = 0;
    int bad = 0;
    std::vector<double> secs;
    std::vector<int> eps;
};

struct DurationScan {
    int fileCount = 0;
    int readableCount = 0;
    long total = 0;
    std::vector<std::wstring> unreadableFiles;
};

bool VideoDurationSec(const fs::path& path, double& seconds);
bool CountVideoDurations(const fs::path& folder, bool recursive, DurationScan& result,
                         const std::atomic<bool>& cancel,
                         const std::function<void(size_t, size_t)>& progress,
                         std::wstring& error);
bool ScanDrama(const fs::path& folder, bool recursive, DramaScan& result,
               const std::atomic<bool>& cancel,
               const std::function<void(size_t, size_t)>& progress,
               std::wstring& error);

} // namespace renamer
