#include "renamer_archives.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace renamer {
namespace {

std::wstring Lower(std::wstring value) {
    for (auto& c : value) c = (wchar_t)towlower(c);
    return value;
}

bool EndsWith(const std::wstring& value, const wchar_t* suffix) {
    const std::wstring s(suffix);
    return value.size() >= s.size() &&
           _wcsicmp(value.c_str() + value.size() - s.size(), s.c_str()) == 0;
}

uint16_t U16(const unsigned char* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t U32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t U64(const unsigned char* p) {
    return (uint64_t)U32(p) | ((uint64_t)U32(p + 4) << 32);
}

bool ReadAt(HANDLE file, uint64_t offset, void* buffer, DWORD bytes) {
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) return false;
    DWORD read = 0;
    return ReadFile(file, buffer, bytes, &read, nullptr) && read == bytes;
}

std::wstring DecodeBytes(const std::string& bytes, bool utf8) {
    if (bytes.empty()) return L"";
    auto convert = [&](UINT page, DWORD flags) -> std::wstring {
        int count = MultiByteToWideChar(page, flags, bytes.data(), (int)bytes.size(), nullptr, 0);
        if (count <= 0) return L"";
        std::wstring result((size_t)count, L'\0');
        if (MultiByteToWideChar(page, flags, bytes.data(), (int)bytes.size(),
                                &result[0], count) != count) return L"";
        return result;
    };
    if (utf8) {
        std::wstring decoded = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
        if (!decoded.empty()) return decoded;
    }
    std::wstring decoded = convert(CP_ACP, 0);
    if (!decoded.empty()) return decoded;
    decoded = convert(CP_OEMCP, 0);
    if (!decoded.empty()) return decoded;
    decoded = convert(437, 0);
    return decoded.empty() ? L"" : decoded;
}

uint32_t ZipCrc32(const std::string& bytes) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool IsVideoMember(const std::wstring& name) {
    size_t slash = name.find_last_of(L"/\\");
    std::wstring leaf = slash == std::wstring::npos ? name : name.substr(slash + 1);
    size_t dot = leaf.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    return IsVideoExt(Lower(leaf.substr(dot)));
}

void AddEpisode(const std::wstring& name, std::set<long>& episodes) {
    if (!IsVideoMember(name)) return;
    EpResult parsed = ParseEpisode(name);
    if (parsed.ep > 0) episodes.insert(parsed.ep);
}

bool ReadZipEntries(const fs::path& path, std::set<long>& episodes, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"无法打开压缩包（系统错误 " + std::to_wstring(GetLastError()) + L"）。";
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 22) {
        CloseHandle(file);
        error = L"ZIP 文件为空或格式不完整。";
        return false;
    }

    const uint64_t tailSize = (uint64_t)std::min<LONGLONG>(fileSize.QuadPart, 22 + 65535);
    std::vector<unsigned char> tail((size_t)tailSize);
    if (!ReadAt(file, (uint64_t)fileSize.QuadPart - tailSize, tail.data(), (DWORD)tail.size())) {
        CloseHandle(file);
        error = L"无法读取 ZIP 目录信息。";
        return false;
    }

    size_t eocd = std::string::npos;
    for (size_t i = tail.size() - 22 + 1; i-- > 0;) {
        if (U32(tail.data() + i) != 0x06054b50) continue;
        uint16_t comment = U16(tail.data() + i + 20);
        if (i + 22 + comment <= tail.size()) { eocd = i; break; }
    }
    if (eocd == std::string::npos) {
        CloseHandle(file);
        error = L"ZIP 文件缺少中央目录，可能已损坏。";
        return false;
    }

    uint16_t disk = U16(tail.data() + eocd + 4);
    uint16_t cdDisk = U16(tail.data() + eocd + 6);
    uint16_t diskEntries = U16(tail.data() + eocd + 8);
    uint64_t entryCount = U16(tail.data() + eocd + 10);
    uint64_t cdSize = U32(tail.data() + eocd + 12);
    uint64_t cdOffset = U32(tail.data() + eocd + 16);
    uint64_t eocdOffset = (uint64_t)fileSize.QuadPart - tailSize + eocd;

    if (disk != 0 || cdDisk != 0 || diskEntries != entryCount) {
        CloseHandle(file);
        error = L"分卷 ZIP 暂不支持扫描。";
        return false;
    }

    if (entryCount == 0xffff || cdSize == 0xffffffffu || cdOffset == 0xffffffffu) {
        if (eocdOffset < 20) {
            CloseHandle(file);
            error = L"ZIP64 目录信息不完整。";
            return false;
        }
        unsigned char locator[20]{};
        if (!ReadAt(file, eocdOffset - sizeof(locator), locator, sizeof(locator)) ||
            U32(locator) != 0x07064b50 || U32(locator + 4) != 0 || U32(locator + 16) != 1) {
            CloseHandle(file);
            error = L"无法读取 ZIP64 目录信息。";
            return false;
        }
        unsigned char zip64[56]{};
        if (!ReadAt(file, U64(locator + 8), zip64, sizeof(zip64)) ||
            U32(zip64) != 0x06064b50 || U32(zip64 + 16) != 0 || U32(zip64 + 20) != 0) {
            CloseHandle(file);
            error = L"ZIP64 文件跨分卷或目录格式无效。";
            return false;
        }
        entryCount = U64(zip64 + 32);
        cdSize = U64(zip64 + 40);
        cdOffset = U64(zip64 + 48);
    }

    constexpr uint64_t kMaxEntries = 1000000;
    constexpr uint64_t kMaxDirectoryBytes = 256ull * 1024 * 1024;
    if (entryCount > kMaxEntries || cdSize > kMaxDirectoryBytes ||
        cdOffset > (uint64_t)fileSize.QuadPart ||
        cdSize > (uint64_t)fileSize.QuadPart - cdOffset) {
        CloseHandle(file);
        error = L"ZIP 中央目录超出支持范围或格式无效。";
        return false;
    }

    std::vector<unsigned char> directory((size_t)cdSize);
    if (cdSize && !ReadAt(file, cdOffset, directory.data(), (DWORD)directory.size())) {
        CloseHandle(file);
        error = L"无法读取 ZIP 中央目录。";
        return false;
    }
    CloseHandle(file);

    size_t offset = 0;
    for (uint64_t i = 0; i < entryCount; ++i) {
        if (offset + 46 > directory.size() || U32(directory.data() + offset) != 0x02014b50) {
            error = L"ZIP 中央目录条目格式无效。";
            return false;
        }
        uint16_t flags = U16(directory.data() + offset + 8);
        uint16_t nameLen = U16(directory.data() + offset + 28);
        uint16_t extraLen = U16(directory.data() + offset + 30);
        uint16_t commentLen = U16(directory.data() + offset + 32);
        size_t recordSize = 46ull + nameLen + extraLen + commentLen;
        if (recordSize > directory.size() - offset) {
            error = L"ZIP 中央目录条目长度无效。";
            return false;
        }
        std::string encoded((const char*)directory.data() + offset + 46, nameLen);
        std::wstring name;
        size_t extraPos = offset + 46 + nameLen;
        size_t extraEnd = extraPos + extraLen;
        while (!(flags & (1u << 11)) && extraPos + 4 <= extraEnd) {
            uint16_t tag = U16(directory.data() + extraPos);
            uint16_t size = U16(directory.data() + extraPos + 2);
            extraPos += 4;
            if (size > extraEnd - extraPos) break;
            if (tag == 0x7075 && size >= 5 && directory[extraPos] == 1 &&
                U32(directory.data() + extraPos + 1) == ZipCrc32(encoded)) {
                std::string unicodeName((const char*)directory.data() + extraPos + 5, size - 5);
                name = DecodeBytes(unicodeName, true);
                if (!name.empty()) break;
            }
            extraPos += size;
        }
        if (name.empty()) name = DecodeBytes(encoded, (flags & (1u << 11)) != 0);
        if (!name.empty()) AddEpisode(name, episodes);
        offset += recordSize;
    }
    return true;
}

std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out += c;
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}

bool FindTar(std::wstring& tarPath) {
    wchar_t systemDir[MAX_PATH + 1]{};
    UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (length && length < MAX_PATH) {
        fs::path candidate = fs::path(systemDir) / L"tar.exe";
        DWORD attrs = GetFileAttributesW(candidate.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            tarPath = candidate.wstring();
            return true;
        }
    }
    wchar_t found[MAX_PATH + 1]{};
    DWORD n = SearchPathW(nullptr, L"tar.exe", nullptr, MAX_PATH, found, nullptr);
    if (n && n < MAX_PATH) { tarPath.assign(found, n); return true; }
    return false;
}

bool ListWithTar(const fs::path& path, std::vector<std::wstring>& names,
                 std::wstring& error, const std::atomic<bool>& cancel) {
    std::wstring tar;
    if (!FindTar(tar)) {
        error = L"读取 RAR/7Z/TAR 需要 Windows 系统提供 tar.exe。";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        error = L"无法创建压缩包读取管道。";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        error = L"无法准备压缩包读取进程的标准输入。";
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullInput;
    PROCESS_INFORMATION process{};
    std::wstring command = QuoteArg(tar) + L" -tf " + QuoteArg(path.wstring());
    BOOL started = CreateProcessW(tar.c_str(), &command[0], nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(nullInput);
    CloseHandle(writePipe);
    if (!started) {
        DWORD code = GetLastError();
        CloseHandle(readPipe);
        error = L"无法启动 tar.exe（系统错误 " + std::to_wstring(code) + L"）。";
        return false;
    }

    std::string output;
    char chunk[8192];
    bool tooLarge = false;
    bool timedOut = false;
    bool wasCancelled = false;
    ULONGLONG startedAt = GetTickCount64();
    for (;;) {
        if (cancel.load()) { wasCancelled = true; break; }
        if (GetTickCount64() - startedAt > 30000) { timedOut = true; break; }
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) {
            DWORD wait = WaitForSingleObject(process.hProcess, 50);
            if (wait == WAIT_OBJECT_0) {
                if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                    break;
            }
            if (GetTickCount64() - startedAt > 30000) { timedOut = true; break; }
            continue;
        }
        DWORD read = 0;
        if (!ReadFile(readPipe, chunk, std::min<DWORD>(sizeof(chunk), available), &read, nullptr) || read == 0)
            break;
        if (output.size() + read > 64ull * 1024 * 1024) {
            tooLarge = true;
            break;
        }
        output.append(chunk, read);
    }
    if (wasCancelled || timedOut || tooLarge)
        TerminateProcess(process.hProcess, wasCancelled ? ERROR_CANCELLED : ERROR_BUFFER_OVERFLOW);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = ERROR_SUCCESS;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wasCancelled) { error = L"扫描已取消。"; return false; }
    if (timedOut) { error = L"读取压缩包内容超时（30 秒）。"; return false; }
    if (tooLarge) {
        error = L"压缩包目录清单过大，超过 64 MB。";
        return false;
    }
    if (exitCode != 0) {
        error = L"系统 tar.exe 无法读取此压缩包（退出代码 " + std::to_wstring(exitCode) + L"）。";
        return false;
    }

    std::wstring decoded = DecodeBytes(output, true);
    if (decoded.empty() && !output.empty()) decoded = DecodeBytes(output, false);
    size_t pos = 0;
    while (pos < decoded.size()) {
        size_t end = decoded.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos) end = decoded.size();
        if (end > pos) names.push_back(decoded.substr(pos, end - pos));
        pos = end;
        while (pos < decoded.size() && (decoded[pos] == L'\r' || decoded[pos] == L'\n')) ++pos;
    }
    return true;
}

bool IsSplitArchive(const fs::path& path) {
    const std::wstring filename = Lower(path.filename().wstring());
    if (EndsWith(filename, L".7z.001") || EndsWith(filename, L".zip.001")) return true;
    size_t part = filename.rfind(L".part");
    if (part != std::wstring::npos && EndsWith(filename, L".rar")) {
        size_t numberStart = part + 5;
        size_t numberEnd = filename.find(L".rar", numberStart);
        if (numberEnd != std::wstring::npos && numberEnd > numberStart) {
            long n = _wtoi(filename.substr(numberStart, numberEnd - numberStart).c_str());
            if (n > 0) return true;
        }
    }
    if (EndsWith(filename, L".rar")) {
        std::wstring base = path.parent_path().wstring() + L"\\" + path.stem().wstring();
        for (int i = 0; i < 100; ++i) {
            wchar_t suffix[16]{};
            swprintf(suffix, 16, L".r%02d", i);
            DWORD attrs = GetFileAttributesW((base + suffix).c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
        }
    }
    if (EndsWith(filename, L".zip")) {
        std::wstring stem = path.stem().wstring();
        std::wstring base = path.parent_path().wstring() + L"\\" + stem;
        for (int i = 1; i <= 99; ++i) {
            wchar_t suffix[16]{};
            swprintf(suffix, 16, L".z%02d", i);
            DWORD attrs = GetFileAttributesW((base + suffix).c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
        }
    }
    return false;
}

void ScanOne(ArchiveScanItem& item, const std::atomic<bool>& cancel) {
    if (IsSplitArchive(item.source)) {
        item.error = L"检测到分卷压缩包，暂不改名以免破坏分卷关系。";
        return;
    }
    std::set<long> episodes;
    bool ok = false;
    if (EndsWith(Lower(item.source.filename().wstring()), L".zip")) {
        ok = ReadZipEntries(item.source, episodes, item.error);
    } else {
        std::vector<std::wstring> names;
        ok = ListWithTar(item.source, names, item.error, cancel);
        if (ok) for (const auto& name : names) AddEpisode(name, episodes);
    }
    if (ok) item.episodes.assign(episodes.begin(), episodes.end());
}

} // namespace

bool IsArchiveFile(const fs::path& path) {
    const std::wstring name = Lower(path.filename().wstring());
    return EndsWith(name, L".zip") || EndsWith(name, L".rar") ||
           EndsWith(name, L".7z") || EndsWith(name, L".tar") ||
           EndsWith(name, L".tar.gz") || EndsWith(name, L".tar.bz2") ||
           EndsWith(name, L".tar.xz") || EndsWith(name, L".tgz") ||
           EndsWith(name, L".tbz2") || EndsWith(name, L".txz") ||
           EndsWith(name, L".7z.001") || EndsWith(name, L".zip.001");
}

std::wstring ArchiveSuffix(const fs::path& path) {
    const std::wstring name = path.filename().wstring();
    const std::wstring lower = Lower(name);
    const wchar_t* suffixes[] = { L".tar.bz2", L".tar.gz", L".tar.xz", L".7z.001",
                                  L".zip.001", L".tbz2", L".tgz", L".txz",
                                  L".zip", L".rar", L".7z", L".tar" };
    for (const wchar_t* suffix : suffixes) {
        size_t n = wcslen(suffix);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, suffix) == 0)
            return name.substr(name.size() - n);
    }
    return path.extension().wstring();
}

bool ScanArchiveDirectory(const fs::path& root, bool recursive,
                          std::vector<ArchiveScanItem>& items,
                          const std::atomic<bool>& cancel,
                          const std::function<void(size_t, size_t)>& progress,
                          std::wstring& error) {
    items.clear();
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec) {
        error = L"压缩包目录不存在或无法访问：\n" + root.wstring();
        return false;
    }

    auto add = [&](const fs::directory_entry& entry) {
        std::error_code typeError;
        if (entry.is_regular_file(typeError) && !typeError && IsArchiveFile(entry.path())) {
            ArchiveScanItem item;
            item.source = entry.path();
            items.push_back(std::move(item));
        }
    };

    if (recursive) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end && !cancel.load(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            add(*it);
        }
    } else {
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end && !cancel.load(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            add(*it);
        }
    }

    std::sort(items.begin(), items.end(), [](const ArchiveScanItem& a, const ArchiveScanItem& b) {
        return NaturalCompare(a.source.wstring(), b.source.wstring()) < 0;
    });
    if (items.empty() && !cancel.load()) {
        error = L"所选目录中没有找到支持的压缩包（ZIP、RAR、7Z、TAR）。";
        return true;
    }
    for (size_t i = 0; i < items.size() && !cancel.load(); ++i) {
        ScanOne(items[i], cancel);
        if (progress) progress(i + 1, items.size());
    }
    return true;
}

} // namespace renamer
