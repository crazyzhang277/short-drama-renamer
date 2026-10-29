#pragma once

#include "renamer_core.h"

#include <atomic>
#include <functional>

namespace jianying_draft {

enum class GroupMode { Folders, FilenameRegex, AllFiles };

struct Group {
    std::wstring name;
    std::vector<renamer::fs::path> clips;
};

struct BuildOptions {
    renamer::fs::path sourceRoot;
    renamer::fs::path outputRoot;
    std::wstring filenameRegex;
    size_t timelinesPerDraft = 50;
};

using Progress = std::function<void(size_t, size_t, const std::wstring&)>;

bool ScanGroups(const renamer::fs::path& root, GroupMode mode,
                const std::wstring& filenameRegex, std::vector<Group>& groups,
                std::wstring& error);
// 把时间线名称改成从 start 开始的连续数字（9、10、11…），不保留原名；start<=0 时不改动。
void NumberGroups(std::vector<Group>& groups, long start);
bool Generate(const BuildOptions& options, const std::vector<Group>& groups,
              const std::atomic<bool>& cancel, const Progress& progress,
              std::vector<renamer::fs::path>& outputs, std::wstring& error);
bool SelfTest(std::wstring& error);

} // namespace jianying_draft
