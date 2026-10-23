#include "renamer_media.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace renamer {

bool VideoDurationSec(const fs::path& path, double& seconds) {
    IMFSourceReader* reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)) || !reader) return false;
    bool ok = false;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                   MF_PD_DURATION, &value)) && value.vt == VT_UI8) {
        seconds = (double)value.uhVal.QuadPart / 10000000.0;
        ok = seconds > 0;
    }
    PropVariantClear(&value);
    reader->Release();
    return ok;
}

bool CountVideoDurations(const fs::path& folder, bool recursive, DurationScan& result,
                         const std::atomic<bool>& cancel,
                         const std::function<void(size_t, size_t)>& progress,
                         std::wstring& error) {
    result = DurationScan();
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) { error = L"文件夹不存在"; return false; }
    std::vector<fs::path> files;
    CollectFiles(folder, recursive, files);
    if (files.empty()) { error = L"该文件夹里没有找到视频文件。"; return false; }
    result.fileCount = (int)files.size();
    for (size_t i = 0; i < files.size(); ++i) {
        if (cancel.load()) { error = L"cancelled"; return false; }
        double seconds = 0;
        if (VideoDurationSec(files[i], seconds)) {
            result.total += (long)(seconds + 0.5);
            ++result.readableCount;
        } else {
            result.unreadableFiles.push_back(files[i].filename().wstring());
        }
        if (progress) progress(i + 1, files.size());
    }
    return true;
}

bool ScanDrama(const fs::path& folder, bool recursive, DramaScan& result,
               const std::atomic<bool>& cancel,
               const std::function<void(size_t, size_t)>& progress,
               std::wstring& error) {
    result = DramaScan();
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) { error = L"文件夹不存在"; return false; }

    std::vector<fs::path> files;
    CollectFiles(folder, recursive, files);
    if (files.empty()) { error = L"没有视频文件"; return false; }

    struct Entry { fs::path path; long episode; };
    std::vector<Entry> entries;
    entries.reserve(files.size());
    int parsed = 0;
    for (const auto& path : files) {
        if (cancel.load()) { error = L"cancelled"; return false; }
        EpResult episode = ParseEpisode(path.filename().wstring());
        if (episode.ep > 0) ++parsed;
        entries.push_back({ path, episode.ep });
    }
    const bool byEpisode = parsed * 10 >= (int)entries.size() * 6;
    std::stable_sort(entries.begin(), entries.end(), [byEpisode](const Entry& a, const Entry& b) {
        if (byEpisode && a.episode != b.episode) return a.episode < b.episode;
        return NaturalCompare(a.path.filename().wstring(), b.path.filename().wstring()) < 0;
    });

    result.count = (int)entries.size();
    result.secs.reserve(entries.size());
    result.eps.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (cancel.load()) { error = L"cancelled"; return false; }
        double seconds = 0;
        bool ok = VideoDurationSec(entries[i].path, seconds);
        result.secs.push_back(ok ? seconds : 0.0);
        result.eps.push_back(byEpisode && entries[i].episode > 0
                                 ? (int)entries[i].episode : (int)i + 1);
        if (ok) result.total += (long)(seconds + 0.5);
        else ++result.bad;
        if (progress) progress(i + 1, entries.size());
    }
    return true;
}

} // namespace renamer
