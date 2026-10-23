#include "renamer_ops.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <system_error>

namespace renamer {
namespace {

std::atomic<unsigned long long> g_unique{0};

std::wstring NativePath(const fs::path& input) {
    std::error_code ec;
    fs::path absolute = fs::absolute(input, ec);
    std::wstring value = (ec ? input : absolute).lexically_normal().wstring();
    if (value.rfind(L"\\\\?\\", 0) == 0) return value;
    if (value.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
}

bool EnsureDirectory(const fs::path& path, DWORD& error) {
    if (path.empty()) return true;
    const std::wstring native = NativePath(path);
    DWORD attrs = GetFileAttributesW(native.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) return true;
        error = ERROR_DIRECTORY;
        return false;
    }
    fs::path parent = path.parent_path();
    if (!parent.empty() && parent.lexically_normal() != path.lexically_normal() &&
        !EnsureDirectory(parent, error)) return false;
    if (CreateDirectoryW(native.c_str(), nullptr)) return true;
    error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        attrs = GetFileAttributesW(native.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return false;
}

bool Exists(const fs::path& path) {
    DWORD attrs = GetFileAttributesW(NativePath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool MoveNoReplace(const fs::path& from, const fs::path& to, DWORD& error) {
    if (MoveFileExW(NativePath(from).c_str(), NativePath(to).c_str(), MOVEFILE_WRITE_THROUGH))
        return true;
    error = GetLastError();
    return false;
}

fs::path UniqueSibling(const fs::path& base, const wchar_t* prefix) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        unsigned long long serial = ++g_unique;
        std::wstring name = std::wstring(prefix) + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()) + L"-" +
                            std::to_wstring(serial) + L".tmp";
        fs::path candidate = base.parent_path() / name;
        if (!Exists(candidate)) return candidate;
    }
    return {};
}

bool WriteNewFile(const fs::path& path, const std::string& data, DWORD& error) {
    HANDLE h = CreateFileW(NativePath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(data.size() - offset, 1u << 20);
        DWORD written = 0;
        if (!WriteFile(h, data.data() + offset, chunk, &written, nullptr) || written != chunk) {
            error = GetLastError();
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && !FlushFileBuffers(h)) { error = GetLastError(); ok = false; }
    CloseHandle(h);
    if (!ok) DeleteFileW(NativePath(path).c_str());
    return ok;
}

bool ReadFileUtf8(const fs::path& path, std::string& out, DWORD& error) {
    HANDLE h = CreateFileW(NativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 64 * 1024 * 1024) {
        error = GetLastError();
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    size_t offset = 0;
    bool ok = true;
    while (offset < out.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - offset, 1u << 20);
        DWORD got = 0;
        if (!ReadFile(h, out.data() + offset, chunk, &got, nullptr) || got == 0) {
            error = GetLastError();
            ok = false;
            break;
        }
        offset += got;
    }
    CloseHandle(h);
    return ok;
}

bool SafeRelative(const fs::path& rel) {
    if (rel.empty() || rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) return false;
    for (const auto& part : rel) if (part == L"..") return false;
    return true;
}

fs::path RelativeTo(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    fs::path absRoot = fs::absolute(root, ec).lexically_normal();
    fs::path absPath = fs::absolute(path, ec).lexically_normal();
    fs::path rel = absPath.lexically_relative(absRoot);
    return rel;
}

struct Transaction {
    fs::path logPath;
    std::wstring phase = L"finalizing";
    std::wstring sourcePhase;
    std::vector<UndoEntry> entries;
};

std::string Serialize(const fs::path& root, const Transaction& tx) {
    std::string data = "\xEF\xBB\xBF# rename-tool undo v2\n";
    data += "# phase=" + W2U8(tx.phase) + "\n";
    data += "# source-phase=" + W2U8(tx.sourcePhase) + "\n";
    data += "# folder=" + W2U8(root.wstring()) + "\n";
    for (const auto& e : tx.entries) {
        data += W2U8(e.from.wstring()) + "\t" + W2U8(e.to.wstring()) + "\t" +
                W2U8(e.temp.wstring()) + "\t" + W2U8(e.undoTemp.wstring()) + "\n";
    }
    return data;
}

bool SaveTransaction(const fs::path& root, const Transaction& tx, DWORD& error) {
    std::wstring tempName = L".undo-write-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++g_unique) + L".tmp";
    fs::path temp = tx.logPath.parent_path() / tempName;
    if (!WriteNewFile(temp, Serialize(root, tx), error)) return false;
    if (!MoveFileExW(NativePath(temp).c_str(), NativePath(tx.logPath).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        DeleteFileW(NativePath(temp).c_str());
        return false;
    }
    return true;
}

bool ResolveEntryPaths(const fs::path& root, const UndoEntry& e,
                       fs::path& from, fs::path& to, fs::path& temp, fs::path& undoTemp) {
    if (!SafeRelative(e.from) || !SafeRelative(e.to) || (!e.temp.empty() && !SafeRelative(e.temp)) ||
        (!e.undoTemp.empty() && !SafeRelative(e.undoTemp))) return false;
    from = root / e.from;
    to = root / e.to;
    if (!e.temp.empty()) temp = root / e.temp;
    if (!e.undoTemp.empty()) undoTemp = root / e.undoTemp;
    return true;
}

bool ReadTransaction(const fs::path& root, const fs::path& logPath, Transaction& tx,
                     std::wstring& message, DWORD& error) {
    std::string raw;
    if (!ReadFileUtf8(logPath, raw, error)) { message = L"无法读取撤销记录。"; return false; }
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw.erase(0, 3);
    std::wstring text = U82W(raw);
    std::wistringstream lines(text);
    std::wstring line;
    bool v2 = false;
    tx = Transaction();
    tx.logPath = logPath;
    tx.phase = L"finalizing";
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line == L"# rename-tool undo v2") { v2 = true; continue; }
        if (line.rfind(L"# phase=", 0) == 0) { tx.phase = line.substr(8); continue; }
        if (line.rfind(L"# source-phase=", 0) == 0) { tx.sourcePhase = line.substr(15); continue; }
        if (line.empty() || line[0] == L'#') continue;
        size_t a = line.find(L'\t');
        if (a == std::wstring::npos) continue;
        size_t b = line.find(L'\t', a + 1);
        if (!v2) {
            // v1 stored current-name first and original-name second.
            UndoEntry e;
            e.to = line.substr(0, a);
            e.from = line.substr(a + 1);
            tx.entries.push_back(e);
            continue;
        }
        size_t c = b == std::wstring::npos ? b : line.find(L'\t', b + 1);
        UndoEntry e;
        e.from = line.substr(0, a);
        e.to = b == std::wstring::npos ? L"" : line.substr(a + 1, b - a - 1);
        e.temp = (b == std::wstring::npos || c == std::wstring::npos) ? L"" : line.substr(b + 1, c - b - 1);
        e.undoTemp = c == std::wstring::npos ? L"" : line.substr(c + 1);
        tx.entries.push_back(e);
    }
    if (tx.entries.empty()) { message = L"撤销记录为空或格式不正确。"; return false; }
    for (const auto& e : tx.entries) {
        fs::path from, to, temp, undoTemp;
        if (!ResolveEntryPaths(root, e, from, to, temp, undoTemp)) {
            message = L"撤销记录包含无效路径，已停止以保护文件。";
            return false;
        }
    }
    if (tx.phase == L"undo-staging" && tx.sourcePhase.empty()) tx.sourcePhase = L"finalizing";
    return true;
}

fs::path CurrentLocation(const fs::path& root, const UndoEntry& e, const std::wstring& phase,
                         const std::wstring& sourcePhase) {
    fs::path from, to, temp, undoTemp;
    if (!ResolveEntryPaths(root, e, from, to, temp, undoTemp)) return {};
    if (phase == L"undo-staging") {
        if (Exists(undoTemp)) return {};
        if (sourcePhase == L"staging") {
            if (!e.temp.empty() && Exists(temp)) return temp;
            return Exists(from) ? from : fs::path();
        }
        if (Exists(to)) return to;
        if (!e.temp.empty() && Exists(temp)) return temp;
        return Exists(from) ? from : fs::path();
    }
    if (phase == L"staging") {
        if (!e.temp.empty() && Exists(temp)) return temp;
        return Exists(from) ? from : fs::path();
    }
    if (Exists(to)) return to;
    if (!e.temp.empty() && Exists(temp)) return temp;
    return Exists(from) ? from : fs::path();
}

bool RestoreOriginals(const fs::path& root, Transaction& tx, DWORD& error, std::wstring& message) {
    if (tx.phase != L"undo-staging" && tx.phase != L"undo-finalizing") {
        tx.sourcePhase = tx.phase;
        tx.phase = L"undo-staging";
        for (auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            if (!ResolveEntryPaths(root, e, from, to, temp, undoTemp)) {
                message = L"撤销记录路径无效。";
                return false;
            }
            if (e.undoTemp.empty()) {
                fs::path unique = UniqueSibling(from, L".rn-undo-");
                if (unique.empty()) { message = L"无法分配撤销临时路径。"; return false; }
                e.undoTemp = RelativeTo(root, unique);
            }
        }
        if (!SaveTransaction(root, tx, error)) {
            message = L"无法保存撤销进度，尚未改动文件。";
            return false;
        }
    }

    if (tx.phase == L"undo-staging") {
        std::vector<fs::path> current;
        current.reserve(tx.entries.size());
        for (const auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            if (!ResolveEntryPaths(root, e, from, to, temp, undoTemp)) {
                message = L"撤销记录路径无效。";
                return false;
            }
            if (Exists(undoTemp)) { current.push_back({}); continue; }
            fs::path p = CurrentLocation(root, e, tx.phase, tx.sourcePhase);
            if (p.empty()) {
                message = L"撤销记录对应的文件找不到：" + to.wstring();
                return false;
            }
            current.push_back(p);
        }

        std::vector<std::wstring> currentKeys;
        for (const auto& p : current) if (!p.empty()) currentKeys.push_back(PathKey(p));
        for (const auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, e, from, to, temp, undoTemp);
            if (Exists(from) && std::find(currentKeys.begin(), currentKeys.end(), PathKey(from)) == currentKeys.end()) {
                message = L"原文件名已被其他文件占用：" + from.wstring() + L"\n撤销已停止，记录仍保留。";
                return false;
            }
        }

        for (size_t i = 0; i < tx.entries.size(); ++i) {
            if (current[i].empty()) continue;
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, tx.entries[i], from, to, temp, undoTemp);
            if (Exists(undoTemp)) continue;
            if (!MoveNoReplace(current[i], undoTemp, error)) {
                message = L"暂存撤销文件失败：" + current[i].wstring() + L"（系统错误 " +
                          std::to_wstring(error) + L"）\n撤销记录已保留。";
                return false;
            }
        }
        tx.phase = L"undo-finalizing";
        if (!SaveTransaction(root, tx, error)) {
            message = L"已暂存文件，但无法保存撤销进度；记录已保留。";
            return false;
        }
    }

    for (const auto& e : tx.entries) {
        fs::path from, to, temp, undoTemp;
        if (!ResolveEntryPaths(root, e, from, to, temp, undoTemp)) {
            message = L"撤销记录路径无效。";
            return false;
        }
        if (!Exists(undoTemp)) {
            if (Exists(from)) continue;
            message = L"撤销中断后找不到待恢复文件：" + from.wstring();
            return false;
        }
        if (!MoveNoReplace(undoTemp, from, error)) {
            message = L"恢复原文件名失败：" + from.wstring() + L"（系统错误 " +
                      std::to_wstring(error) + L"）\n撤销记录已保留，可再次尝试撤销。";
            return false;
        }
    }
    DeleteFileW(NativePath(tx.logPath).c_str());
    return true;
}

} // namespace

bool WriteUtf8Atomic(const fs::path& path, const std::string& data, std::wstring& errorMessage) {
    DWORD directoryError = ERROR_SUCCESS;
    if (!EnsureDirectory(path.parent_path(), directoryError)) {
        errorMessage = L"无法创建文件所在目录，系统错误 " + std::to_wstring(directoryError);
        return false;
    }
    fs::path temp = UniqueSibling(path, L".write-");
    if (temp.empty()) { errorMessage = L"无法分配临时文件名。"; return false; }
    DWORD error = ERROR_SUCCESS;
    if (!WriteNewFile(temp, data, error)) {
        errorMessage = L"无法写入临时文件，系统错误 " + std::to_wstring(error);
        return false;
    }
    if (!MoveFileExW(NativePath(temp).c_str(), NativePath(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        DeleteFileW(NativePath(temp).c_str());
        errorMessage = L"无法替换目标文件，系统错误 " + std::to_wstring(error);
        return false;
    }
    return true;
}

OperationResult ExecuteRename(const fs::path& root, const std::vector<PlanItem>& plan) {
    OperationResult result;
    std::vector<const PlanItem*> work;
    std::vector<std::wstring> problems;
    std::vector<std::wstring> targets;
    std::vector<std::wstring> changedSources;
    for (const auto& item : plan) {
        if (!Exists(item.src)) problems.push_back(L"源文件不存在：" + item.src.wstring());
        if (SamePath(item.src, item.target)) continue;
        if (item.dup) problems.push_back(L"新名重复：" + item.target.wstring());
        targets.push_back(PathKey(item.target));
        changedSources.push_back(PathKey(item.src));
        work.push_back(&item);
        if (NativePath(item.target).size() >= 32767)
            problems.push_back(L"路径超出 Windows 支持范围：" + item.target.wstring());
    }
    std::sort(targets.begin(), targets.end());
    if (std::adjacent_find(targets.begin(), targets.end()) != targets.end())
        problems.push_back(L"多个文件会改成同一个目标路径。");
    for (const auto* item : work) {
        if (Exists(item->target) &&
            std::find(changedSources.begin(), changedSources.end(), PathKey(item->target)) == changedSources.end())
            problems.push_back(L"目标文件已存在：" + item->target.wstring());
    }
    if (!problems.empty()) {
        result.message = L"检测到冲突，操作已取消：\n";
        for (size_t i = 0; i < problems.size() && i < 8; ++i)
            result.message += L"\n" + problems[i];
        if (problems.size() > 8) result.message += L"\n……";
        return result;
    }
    if (work.empty()) { result.ok = true; return result; }

    fs::path stateDir = root / L".rename-tool";
    DWORD stateError = ERROR_SUCCESS;
    if (!EnsureDirectory(stateDir, stateError)) {
        result.win32Error = stateError;
        result.message = L"无法创建 .rename-tool 目录，系统错误 " +
                         std::to_wstring(stateError) + L"。";
        return result;
    }

    Transaction tx;
    tx.phase = L"staging";
    for (const auto* item : work) {
        UndoEntry e;
        e.from = RelativeTo(root, item->src);
        e.to = RelativeTo(root, item->target);
        fs::path tempAbs = UniqueSibling(item->src, L".rn-tmp-");
        fs::path undoTempAbs = UniqueSibling(item->src, L".rn-undo-");
        e.temp = RelativeTo(root, tempAbs);
        e.undoTemp = RelativeTo(root, undoTempAbs);
        if (tempAbs.empty() || undoTempAbs.empty() ||
            NativePath(tempAbs).size() >= 32767 || NativePath(undoTempAbs).size() >= 32767 ||
            !SafeRelative(e.from) || !SafeRelative(e.to) || !SafeRelative(e.temp) ||
            !SafeRelative(e.undoTemp)) {
            result.message = L"无法生成安全的相对路径撤销记录。";
            return result;
        }
        tx.entries.push_back(e);
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        fs::path candidate = stateDir / (L"undo-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++g_unique) + L".txt");
        if (!Exists(candidate)) { tx.logPath = candidate; break; }
    }
    if (tx.logPath.empty()) { result.message = L"无法创建唯一的撤销记录名。"; return result; }
    DWORD error = ERROR_SUCCESS;
    if (!SaveTransaction(root, tx, error)) {
        result.win32Error = error;
        result.message = L"无法写入改名操作记录，未执行改名。";
        return result;
    }

    std::wstring rollbackMessage;
    auto rollback = [&]() {
        DWORD restoreError = ERROR_SUCCESS;
        rollbackMessage.clear();
        bool clean = RestoreOriginals(root, tx, restoreError, rollbackMessage);
        if (!clean && restoreError != ERROR_SUCCESS) result.win32Error = restoreError;
        return clean;
    };

    for (size_t i = 0; i < tx.entries.size(); ++i) {
        fs::path from, to, temp, undoTemp;
        ResolveEntryPaths(root, tx.entries[i], from, to, temp, undoTemp);
        if (!MoveNoReplace(from, temp, error)) {
            result.win32Error = error;
            bool clean = rollback();
            result.message = L"无法暂存文件：" + from.wstring() +
                L"\n系统错误 " + std::to_wstring(error) +
                (clean ? L"\n已恢复原状。" : L"\n自动恢复未完成，撤销记录已保留。再点「撤销上次」可继续恢复。\n" + rollbackMessage);
            return result;
        }
    }
    tx.phase = L"finalizing";
    if (!SaveTransaction(root, tx, error)) {
        result.win32Error = error;
        bool clean = rollback();
        result.message = L"更新改名记录失败，" + std::wstring(clean ? L"文件已恢复原状。" : L"恢复未完成，撤销记录已保留。\n" + rollbackMessage);
        return result;
    }
    for (size_t i = 0; i < tx.entries.size(); ++i) {
        fs::path from, to, temp, undoTemp;
        ResolveEntryPaths(root, tx.entries[i], from, to, temp, undoTemp);
        if (!MoveNoReplace(temp, to, error)) {
            result.win32Error = error;
            bool clean = rollback();
            result.message = L"无法完成文件改名：" + to.wstring() +
                L"\n系统错误 " + std::to_wstring(error) +
                (clean ? L"\n已恢复原状。" : L"\n自动恢复未完成，撤销记录已保留。再点「撤销上次」可继续恢复。\n" + rollbackMessage);
            return result;
        }
    }
    result.ok = true;
    result.changed = work.size();
    result.logPath = tx.logPath;
    return result;
}

OperationResult UndoLatest(const fs::path& root) {
    OperationResult result;
    fs::path stateDir = root / L".rename-tool";
    std::error_code ec;
    if (!fs::is_directory(stateDir, ec)) { result.message = L"该文件夹里没有撤销记录。"; return result; }
    fs::path latest;
    fs::file_time_type latestTime{};
    bool found = false;
    for (fs::directory_iterator it(stateDir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const auto name = it->path().filename().wstring();
        if (name.rfind(L"undo-", 0) != 0 || it->path().extension() != L".txt") continue;
        auto t = fs::last_write_time(it->path(), e2);
        if (!e2 && (!found || t > latestTime)) { found = true; latestTime = t; latest = it->path(); }
    }
    if (!found) { result.message = L"没有找到撤销记录。"; return result; }

    Transaction tx;
    if (!ReadTransaction(root, latest, tx, result.message, result.win32Error)) return result;

    if (tx.phase == L"undo-finalizing") {
        for (const auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, e, from, to, temp, undoTemp);
            if (!Exists(undoTemp)) {
                if (Exists(from)) continue;
                result.message = L"撤销中断后找不到待恢复文件：" + from.wstring();
                return result;
            }
            DWORD error = ERROR_SUCCESS;
            if (!MoveNoReplace(undoTemp, from, error)) {
                result.win32Error = error;
                result.message = L"继续撤销失败：" + from.wstring() + L"（系统错误 " + std::to_wstring(error) + L"）";
                return result;
            }
            ++result.changed;
        }
    } else {
        if (tx.phase != L"undo-staging") {
            tx.sourcePhase = tx.phase;
            tx.phase = L"undo-staging";
            for (auto& e : tx.entries) {
                fs::path from, to, temp, undoTemp;
                ResolveEntryPaths(root, e, from, to, temp, undoTemp);
                fs::path unique = UniqueSibling(from, L".rn-undo-");
                if (unique.empty()) { result.message = L"无法分配撤销临时路径。"; return result; }
                e.undoTemp = RelativeTo(root, unique);
            }
            DWORD error = ERROR_SUCCESS;
            if (!SaveTransaction(root, tx, error)) {
                result.win32Error = error;
                result.message = L"无法保存撤销进度，尚未改动文件。";
                return result;
            }
        }

        std::vector<fs::path> current;
        current.reserve(tx.entries.size());
        for (auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, e, from, to, temp, undoTemp);
            if (Exists(undoTemp)) { current.push_back({}); continue; }
            fs::path p = CurrentLocation(root, e, tx.phase, tx.sourcePhase);
            if (p.empty()) {
                result.message = L"撤销记录对应的文件找不到：" + to.wstring();
                return result;
            }
            current.push_back(p);
        }
        std::vector<std::wstring> currentKeys;
        for (const auto& p : current) if (!p.empty()) currentKeys.push_back(PathKey(p));
        for (const auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, e, from, to, temp, undoTemp);
            if (Exists(from) && std::find(currentKeys.begin(), currentKeys.end(), PathKey(from)) == currentKeys.end()) {
                result.message = L"原文件名已被其他文件占用：" + from.wstring() + L"\n撤销已停止，记录仍保留。";
                return result;
            }
        }
        for (size_t i = 0; i < tx.entries.size(); ++i) {
            if (current[i].empty()) continue;
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, tx.entries[i], from, to, temp, undoTemp);
            if (Exists(undoTemp)) continue;
            DWORD error = ERROR_SUCCESS;
            if (!MoveNoReplace(current[i], undoTemp, error)) {
                result.win32Error = error;
                result.message = L"暂存撤销文件失败：" + current[i].wstring() + L"（系统错误 " +
                                std::to_wstring(error) + L"）\n撤销记录已保留。";
                return result;
            }
        }
        tx.phase = L"undo-finalizing";
        DWORD error = ERROR_SUCCESS;
        if (!SaveTransaction(root, tx, error)) {
            result.win32Error = error;
            result.message = L"已暂存文件，但无法保存撤销进度；记录已保留。";
            return result;
        }
        for (const auto& e : tx.entries) {
            fs::path from, to, temp, undoTemp;
            ResolveEntryPaths(root, e, from, to, temp, undoTemp);
            if (!MoveNoReplace(undoTemp, from, error)) {
                result.win32Error = error;
                result.message = L"恢复原文件名失败：" + from.wstring() + L"（系统错误 " +
                                std::to_wstring(error) + L"）\n撤销记录已保留，可再次尝试撤销。";
                return result;
            }
            ++result.changed;
        }
    }

    DeleteFileW(NativePath(latest).c_str());
    result.ok = true;
    result.logPath = latest;
    return result;
}

} // namespace renamer
