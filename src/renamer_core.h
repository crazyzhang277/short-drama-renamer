#pragma once

#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

namespace renamer {

namespace fs = std::filesystem;

struct EpResult {
    long ep = 0;
    int score = -1;
};

struct PlanItem {
    fs::path src;
    fs::path relativeSrc;
    fs::path target;
    fs::file_time_type mtime{};
    bool hasMtime = false;
    std::wstring oldName;
    std::wstring displayName;
    std::wstring newName;
    long ep = 0;
    int score = -1;
    bool dup = false;
};

struct UndoEntry {
    fs::path from;
    fs::path to;
    fs::path temp;
    fs::path undoTemp;
};

struct DramaNameGuess {
    std::wstring title;
};

std::wstring Trim(const std::wstring& s);
std::string W2U8(const std::wstring& w);
std::wstring U82W(const std::string& s);
std::wstring LowerExt(const fs::path& p);
bool IsVideoExt(const std::wstring& ext);
bool IsBadNumber(const std::wstring& s);
EpResult ParseEpisode(const std::wstring& raw);
int NaturalCompare(const std::wstring& a, const std::wstring& b);
std::wstring NormalizePunct(const std::wstring& in);
std::wstring SanitizeForFile(const std::wstring& in);
std::wstring CollapseSpaces(const std::wstring& in);
std::wstring ApplyColon(const std::wstring& s, int mode);
bool IsNoiseSegment(const std::wstring& raw);
std::wstring FolderNameToTitle(const std::wstring& raw);
std::wstring GuessTitleFromFolder(const std::wstring& folder);
DramaNameGuess GuessDramaNamesFromFolder(const std::wstring& folder);
std::wstring BuildNewName(const std::wstring& titleRaw, long num, int pad,
                          const std::wstring& typeTag, int colonMode,
                          const std::wstring& ext);
void CollectFiles(const fs::path& dir, bool sub, std::vector<fs::path>& out);
void InvalidateScanCache();

bool MakePlan(const std::wstring& folder, const std::wstring& titleRaw,
              long start, int pad, const std::wstring& tag, int cm,
              int mode, bool sub, bool keepEp,
              std::vector<PlanItem>& items, std::wstring& err);

std::wstring PathKey(const fs::path& path);
bool SamePath(const fs::path& a, const fs::path& b);

} // namespace renamer
