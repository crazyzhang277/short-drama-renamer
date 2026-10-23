#include "renamer_core.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <regex>
#include <system_error>

namespace renamer {
std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string W2U8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring U82W(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring LowerExt(const fs::path& p) {
    std::wstring e = p.extension().wstring();
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

bool IsVideoExt(const std::wstring& ext) {
    static const wchar_t* exts[] = {
        L".mp4", L".mkv", L".mov", L".avi", L".wmv", L".flv", L".m4v",
        L".ts",  L".webm", L".mpg", L".mpeg", L".rmvb", L".rm", L".3gp",
        L".m2ts", L".vob", L".ogv", L".divx", L".asf", L".f4v"
    };
    for (auto e : exts) if (ext == e) return true;
    return false;
}

// ------------------------------------------------------------------ 集数识别
bool IsBadNumber(const std::wstring& s) {
    static const wchar_t* bad[] = {
        L"240", L"360", L"480", L"576", L"720", L"1080", L"1280",
        L"1440", L"1920", L"2160", L"4320", L"264", L"265"
    };
    for (auto b : bad) if (s == b) return true;
    if (s.size() == 4) {
        int v = _wtoi(s.c_str());
        if (v >= 1900 && v <= 2099) return true;
    }
    return false;
}



EpResult ParseEpisode(const std::wstring& raw) {
    std::wstring name = raw;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);

    EpResult best;

    static const std::wregex reS01E(
        L"[Ss]\\d{1,2}[\\s._-]*[Ee][Pp]?\\s*(\\d{1,4})",
        std::regex_constants::icase);
    static const std::wregex reChinese(
        L"第\\s*(\\d{1,4})\\s*[集话話期]",
        std::regex_constants::icase);
    static const std::wregex reChineseShort(
        L"(\\d{1,4})\\s*[集话話]",
        std::regex_constants::icase);
    static const std::wregex reEp(
        L"(?:^|[^A-Za-z])[Ee][Pp][\\s._-]*(\\d{1,4})",
        std::regex_constants::icase);
    static const std::wregex reBracket(
        L"[\\[\\(（【]\\s*(\\d{1,4})\\s*[\\]\\)）】]",
        std::regex_constants::icase);
    static const std::wregex reTail(
        L"(?:^|[^\\d])(\\d{1,4})\\s*$",
        std::regex_constants::icase);
    static const std::wregex reHead(
        L"^\\s*(\\d{1,4})(?:[^\\d]|$)",
        std::regex_constants::icase);

    auto tryPat = [&](const std::wregex& re, int score, bool filterBad) {
        try {
            std::wsregex_iterator it(name.begin(), name.end(), re), end;
            long val = -1;
            for (; it != end; ++it) {
                if (it->size() < 2) continue;
                std::wstring g = (*it)[1].str();
                if (filterBad && IsBadNumber(g)) continue;
                long v = _wtoi(g.c_str());
                if (v <= 0) continue;
                val = v;   // 取最右匹配
            }
            if (val > 0 && score > best.score) { best.ep = val; best.score = score; }
        } catch (...) {}
    };

    // S01E05 / S01.E05 / s1e5
    try {
        std::wsmatch m;
        if (std::regex_search(name, m, reS01E) && m.size() > 1) {
            long v = _wtoi(m[1].str().c_str());
            if (v > 0 && best.score < 120) { best.ep = v; best.score = 120; }
        }
    } catch (...) {}

    tryPat(reChinese,      130, false);
    tryPat(reChineseShort, 125, false);
    tryPat(reEp,           110, true);
    tryPat(reBracket,        80, true);
    tryPat(reTail,           60, true);
    tryPat(reHead,           50, true);

    return best;
}

// ------------------------------------------------------------------ 自然排序
int NaturalCompare(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (iswdigit(a[i]) && iswdigit(b[j])) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && iswdigit(a[i2])) i2++;
            while (j2 < b.size() && iswdigit(b[j2])) j2++;
            std::wstring na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
            size_t p = na.find_first_not_of(L'0');
            std::wstring ta = (p == std::wstring::npos) ? L"0" : na.substr(p);
            p = nb.find_first_not_of(L'0');
            std::wstring tb = (p == std::wstring::npos) ? L"0" : nb.substr(p);
            if (ta.size() != tb.size()) return ta.size() < tb.size() ? -1 : 1;
            int c = ta.compare(tb);
            if (c) return c < 0 ? -1 : 1;
            if (na.size() != nb.size()) return na.size() < nb.size() ? -1 : 1;
            i = i2; j = j2;
        } else {
            wchar_t ca = (wchar_t)towlower(a[i]), cb = (wchar_t)towlower(b[j]);
            if (ca != cb) return ca < cb ? -1 : 1;
            i++; j++;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

// ------------------------------------------------------------------ 剧名清洗
std::wstring NormalizePunct(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c = in[i];
        switch (c) {
            case L'，': case L'、': out += L','; break;
            case L'。': out += L'.'; break;
            case L'：': out += L':'; break;
            case L'；': out += L';'; break;
            case L'！': out += L'!'; break;
            case L'？': out += L'?'; break;
            case L'（': out += L'('; break;
            case L'）': out += L')'; break;
            case L'【': out += L'['; break;
            case L'】': out += L']'; break;
            case L'‘': case L'’': case L'＇': out += L'\''; break;
            case L'“': case L'”': case L'＂': out += L'"'; break;
            case L'－': case L'—': case L'–': case L'﹣': out += L'-'; break;
            case L'　': out += L' '; break;
            default: out += c;
        }
    }
    std::wstring res;
    bool sp = false;
    for (wchar_t c : out) {
        if (c == L' ') { if (!sp) res += L' '; sp = true; }
        else { res += c; sp = false; }
    }
    return Trim(res);
}

std::wstring SanitizeForFile(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        if (c < 32) continue;
        if (c == L'<' || c == L'>' || c == L'/' || c == L'\\' || c == L'|' ||
            c == L'?' || c == L'*' || c == L'"') continue;
        out += c;
    }
    while (!out.empty() && (out.back() == L'.' || out.back() == L' ')) out.pop_back();
    return out;
}

std::wstring CollapseSpaces(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool sp = false;
    for (wchar_t c : in) {
        if (c == L' ') { if (!sp) out += L' '; sp = true; }
        else { out += c; sp = false; }
    }
    return Trim(out);
}

std::wstring ApplyColon(const std::wstring& s, int mode) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L':') {
            if (mode == 0)      out += L'：';
            else if (mode == 1) out += L" - ";
            else                out += L' ';
        } else out += c;
    }
    return CollapseSpaces(out);
}

// ------------------------------------------------------------------ 从文件夹名猜剧名
bool HasCJK(const std::wstring& s) {
    for (wchar_t c : s) if (c >= 0x4E00 && c <= 0x9FFF) return true;
    return false;
}

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// 这些是"容器目录"名字，不是剧名：盘符、系统目录、常用中文归类目录、本工具目录
bool IsNoiseSegment(const std::wstring& raw) {
    std::wstring s = Trim(raw);
    if (s.empty()) return true;
    if (s.size() == 2 && s[1] == L':') return true;            // C:  D:
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/' || s.back() == L':'))
        s.pop_back();
    s = Trim(s);
    if (s.empty()) return true;

    static const wchar_t* kNoise[] = {
        L"users", L"user", L"public", L"documents", L"desktop", L"downloads",
        L"videos", L"video", L"movies", L"movie", L"music", L"pictures", L"photos",
        L"temp", L"tmp", L"appdata", L"local", L"locallow", L"roaming",
        L"program files", L"program files (x86)", L"programdata", L"windows",
        L"system32", L"onedrive", L"library", L"home", L"root", L"disk",
        L"new folder", L"new folder (2)", L"untitled", L"output", L"out",
        L"新建文件夹", L"未命名", L"未命名文件夹", L"我的视频", L"我的文档",
        L"视频", L"影片", L"影视", L"短剧", L"短剧视频", L"剧集", L"合集",
        L"下载", L"桌面", L"文档", L"图片", L"音乐", L"收藏夹",
        L"待处理", L"待更名", L"待整理", L"素材", L"资源", L"临时",
        L"自动更名工具", L"更名工具", L"短剧更名", L"短剧更名工具",
    };
    for (auto n : kNoise) if (EqualsNoCase(s, n)) return true;

    // 纯数字 / 纯符号 / 太短没意义（如 "1"、"S01"、"v2"）
    bool hasAlpha = false, cjk = false;
    for (wchar_t c : s) {
        if (iswalpha(c)) {
            hasAlpha = true;
            if (c >= 0x4E00 && c <= 0x9FFF) cjk = true;
        }
    }
    if (!hasAlpha) return true;
    if (!cjk && s.size() <= 3) return true;
    return false;
}

// 单词首字母大写；全大写且很短的词（CEO / TV / FBI）保持原样
std::wstring TitleCaseWords(const std::wstring& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(L' ', i);
        if (j == std::wstring::npos) j = s.size();
        std::wstring w = s.substr(i, j - i);
        bool allUpper = true, hasAlpha = false;
        for (wchar_t c : w) {
            if (iswalpha(c)) { hasAlpha = true; if (!iswupper(c)) allUpper = false; }
        }
        if (hasAlpha && !(allUpper && w.size() <= 4)) {
            bool first = true;
            for (auto& c : w) {
                if (iswalpha(c)) {
                    c = first ? (wchar_t)towupper(c) : (wchar_t)towlower(c);
                    first = false;
                }
            }
        }
        out += w;
        if (j < s.size()) out += L' ';
        i = j + 1;
    }
    return out;
}

// "the_lost-HEIRESS.of time" -> "The Lost Heiress Of Time"
std::wstring FolderNameToTitle(const std::wstring& raw) {
    std::wstring s;
    s.reserve(raw.size());
    for (wchar_t c : raw) {
        if (c == L'_' || c == L'.' || c == L'-' || c == L'　') s += L' ';
        else s += c;
    }
    s = CollapseSpaces(s);
    if (HasCJK(s)) return s;          // 中文名原样保留
    return TitleCaseWords(s);
}

// 从文件夹路径里由深到浅找第一个"像剧名"的目录名；盘符和系统目录一律跳过
std::wstring GuessTitleFromFolder(const std::wstring& folder) {
    if (Trim(folder).empty()) return L"";
    std::error_code ec;
    fs::path p(folder);
    std::vector<std::wstring> segs;
    for (auto& part : p) {
        std::wstring s = part.wstring();
        if (!s.empty()) segs.push_back(s);
    }
    for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
        if (IsNoiseSegment(*it)) continue;
        std::wstring t = FolderNameToTitle(Trim(*it));
        if (!t.empty()) return t;
    }
    return L"";
}

namespace {

std::wstring LowerWide(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

bool IsSmartNoise(const std::wstring& raw) {
    std::wstring s = LowerWide(Trim(raw));
    if (s.size() >= 2) {
        wchar_t first = s.front(), last = s.back();
        bool wrapped = (first == L'(' && last == L')') ||
                       (first == L'[' && last == L']') ||
                       (first == L'（' && last == L'）') ||
                       (first == L'【' && last == L'】');
        if (wrapped) s = Trim(s.substr(1, s.size() - 2));
    }
    if (s.empty() || IsNoiseSegment(s)) return true;
    static const wchar_t* kNoise[] = {
        L"quan", L"全", L"全集", L"全剧", L"完整", L"完整版", L"成片", L"成片文件",
        L"成片视频", L"片", L"full", L"full episode", L"full episodes", L"episodes",
        L"episode", L"ep", L"video", L"videos", L"final", L"output", L"export",
        L"release", L"raw", L"作品", L"作品集", L"成品", L"原版", L"海外版", L"译制版",
        L"英文版", L"中文版", L"中文字幕", L"无水印", L"带字幕", L"字幕",
    };
    for (auto n : kNoise) if (s == n) return true;
    return false;
}

std::wstring StripLeadingCatalogId(std::wstring s) {
    try {
        static const std::wregex re(
            L"^\\s*[A-Za-z]{1,5}\\s*[-_]\\s*\\d{2,8}\\s*[-_]\\s*",
            std::regex_constants::icase);
        s = std::regex_replace(s, re, L"");
    } catch (...) {}
    return Trim(s);
}

std::wstring StripSmartSuffix(std::wstring s) {
    s = Trim(s);
    if (IsSmartNoise(s)) return L"";
    try {
        static const std::wregex suffix(
            L"(?:[\\s._-]+(?:成片(?:文件|视频)?|全集|全剧|完整版|full\\s+episodes?|episodes?|episode|videos?|video|final|output|export|release|raw|原版|海外版|译制版|英文版|中文版|中文字幕|无水印|带字幕|字幕))+\\s*$",
            std::regex_constants::icase);
        for (int i = 0; i < 3; ++i) {
            std::wstring next = std::regex_replace(s, suffix, L"");
            next = Trim(next);
            if (next == s) break;
            s = next;
        }
    } catch (...) {}
    while (!s.empty() && (s.back() == L'-' || s.back() == L'_' || s.back() == L'.' ||
                          s.back() == L' ' || s.back() == L'：' || s.back() == L':'))
        s.pop_back();
    return Trim(s);
}

std::wstring SmartTitleCase(const std::wstring& s) {
    const wchar_t hyphen = L'\uE000';
    std::wstring protectedHyphens = s;
    for (auto& c : protectedHyphens) if (c == L'-') c = hyphen;
    std::wstring out = FolderNameToTitle(protectedHyphens);
    for (auto& c : out) if (c == hyphen) c = L'-';
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == L'-' && iswalpha(out[i + 1]))
            out[i + 1] = (wchar_t)towupper(out[i + 1]);
    }
    return out;
}

std::wstring SmartTitleFromSegment(const std::wstring& raw) {
    std::wstring s = StripLeadingCatalogId(raw);
    s = StripSmartSuffix(s);
    s = NormalizePunct(s);
    for (auto& c : s) if (c == L'_' || c == L'.' || c == L'　') c = L' ';
    s = CollapseSpaces(s);
    if (s.empty() || IsSmartNoise(s)) return L"";
    if (HasCJK(s)) return s;
    bool hasUpper = false, hasLower = false;
    for (wchar_t c : s) {
        if ((c >= L'A' && c <= L'Z') || iswupper(c)) hasUpper = true;
        if ((c >= L'a' && c <= L'z') || iswlower(c)) hasLower = true;
    }
    return hasUpper && hasLower ? s : SmartTitleCase(s);
}

} // namespace

DramaNameGuess GuessDramaNamesFromFolder(const std::wstring& folder) {
    DramaNameGuess result;
    if (Trim(folder).empty()) return result;

    std::error_code ec;
    fs::path p(folder);
    std::vector<std::wstring> segs;
    for (auto& part : p) {
        std::wstring s = part.wstring();
        if (!s.empty()) segs.push_back(s);
    }

    struct Candidate {
        std::wstring title;
        int score = -1;
        size_t depth = 0;
    } best;
    for (size_t i = 0; i < segs.size(); ++i) {
        std::wstring title = SmartTitleFromSegment(segs[i]);
        if (title.empty()) continue;
        int score = 0;
        if (HasCJK(title)) score += 30;
        if (title.size() >= 5) score += 10;
        if (title.find(L' ') != std::wstring::npos || title.find(L'-') != std::wstring::npos)
            score += 5;
        score += (int)i;
        if (score > best.score || (score == best.score && i > best.depth))
            best = { title, score, i };
    }

    if (best.score >= 0) {
        result.title = best.title;
    }
    return result;
}

std::wstring BuildNewName(const std::wstring& titleRaw, long num, int pad,
                                 const std::wstring& typeTag, int colonMode,
                                 const std::wstring& ext) {
    std::wstring title = NormalizePunct(Trim(titleRaw));
    title = ApplyColon(title, colonMode);

    wchar_t numbuf[32] = {0};
    if (pad > 0) swprintf(numbuf, 32, L"%0*ld", pad, num);
    else         swprintf(numbuf, 32, L"%ld", num);

    std::wstring name = title + L" - " + numbuf
                      + L" #short drama #mini drama #micro drama";
    if (!typeTag.empty()) name += L" #" + typeTag;
    name = SanitizeForFile(name);
    if (name.size() > 170) name = name.substr(0, 170);
    return name + ext;
}

std::wstring PathKey(const fs::path& path) {
    std::error_code ec;
    fs::path full = fs::absolute(path, ec);
    if (ec) full = path;
    std::wstring key = full.lexically_normal().wstring();
    for (auto& c : key) c = (wchar_t)towlower(c);
    return key;
}

bool SamePath(const fs::path& a, const fs::path& b) {
    return PathKey(a) == PathKey(b);
}

namespace {
struct ScanCache {
    std::wstring key;
    bool sub = false;
    std::chrono::steady_clock::time_point at{};
    std::vector<PlanItem> items;
};

ScanCache g_scanCache;
constexpr auto kScanCacheTtl = std::chrono::milliseconds(1500);

bool LoadScanCache(const fs::path& dir, bool sub, std::vector<PlanItem>& out) {
    const auto now = std::chrono::steady_clock::now();
    if (g_scanCache.key != PathKey(dir) || g_scanCache.sub != sub ||
        g_scanCache.items.empty() || now - g_scanCache.at > kScanCacheTtl) return false;
    out = g_scanCache.items;
    return true;
}

void StoreScanCache(const fs::path& dir, bool sub, const std::vector<PlanItem>& items) {
    g_scanCache.key = PathKey(dir);
    g_scanCache.sub = sub;
    g_scanCache.at = std::chrono::steady_clock::now();
    g_scanCache.items = items;
}
} // namespace

void InvalidateScanCache() { g_scanCache = ScanCache(); }

// ------------------------------------------------------------------ 枚举
void CollectFiles(const fs::path& dir, bool sub, std::vector<fs::path>& out) {
    out.clear();
    std::error_code ec;
    if (sub) {
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code typeErr;
            if (it->is_directory(typeErr) && !typeErr && it->path().filename() == L".rename-tool") {
                it.disable_recursion_pending();
                continue;
            }
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            fs::path p = it->path();
            if (p.filename() == L".rename-tool") continue;
            if (IsVideoExt(LowerExt(p))) out.push_back(p);
        }
    } else {
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            fs::path p = it->path();
            if (IsVideoExt(LowerExt(p))) out.push_back(p);
        }
    }
}

bool MakePlan(const std::wstring& folder, const std::wstring& titleRaw,
                     long start, int pad, const std::wstring& tag, int cm,
                     int mode, bool sub, bool keepEp,
                     std::vector<PlanItem>& items, std::wstring& err) {
    items.clear();

    if (folder.empty()) { err = L"请先选择视频文件夹。"; return false; }
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) { err = L"文件夹不存在：\n" + folder; return false; }

    std::wstring title = Trim(titleRaw);
    if (title.empty()) { err = L"请输入剧名。"; return false; }

    if (start <= 0) start = 1;
    if (pad < 0) pad = 0;
    if (pad > 4) pad = 4;

    if (!LoadScanCache(fs::path(folder), sub, items)) {
        std::vector<fs::path> files;
        CollectFiles(fs::path(folder), sub, files);
        if (files.empty()) { err = L"该文件夹里没有找到视频文件。"; return false; }
        items.reserve(files.size());
        for (auto& f : files) {
            PlanItem pi;
            pi.src = f;
            pi.relativeSrc = f.lexically_relative(fs::path(folder));
            if (pi.relativeSrc.empty()) pi.relativeSrc = f.filename();
            pi.displayName = pi.relativeSrc.wstring();
            pi.oldName = f.filename().wstring();
            std::error_code mtErr;
            pi.mtime = fs::last_write_time(f, mtErr);
            pi.hasMtime = !mtErr;
            EpResult r = ParseEpisode(pi.oldName);
            pi.ep = r.ep;
            pi.score = r.score;
            items.push_back(pi);
        }
        StoreScanCache(fs::path(folder), sub, items);
    } else if (items.empty()) {
        err = L"该文件夹里没有找到视频文件。";
        return false;
    }

    if (mode == 2) {
        std::sort(items.begin(), items.end(), [](const PlanItem& a, const PlanItem& b) {
            if (a.hasMtime && b.hasMtime && a.mtime != b.mtime) return a.mtime < b.mtime;
            return NaturalCompare(a.oldName, b.oldName) < 0;
        });
    } else {
        size_t recognized = 0;
        for (auto& it : items) if (it.score >= 0) recognized++;
        bool useEp = (mode == 0) && !items.empty()
                   && (recognized * 10 >= items.size() * 6);
        if (useEp) {
            std::sort(items.begin(), items.end(), [](const PlanItem& a, const PlanItem& b) {
                if (a.ep != b.ep) return a.ep < b.ep;
                if (a.score != b.score) return a.score > b.score;
                return NaturalCompare(a.oldName, b.oldName) < 0;
            });
        } else {
            std::sort(items.begin(), items.end(), [](const PlanItem& a, const PlanItem& b) {
                return NaturalCompare(a.oldName, b.oldName) < 0;
            });
        }
    }

    long n = start;
    for (auto& it : items) {
        // 默认：按位置连续编号（断档会被自动接上）
        // keepEp：沿用从文件名里识别出的原集数（断档保留），认不出时才退回连续编号
        long num = n;
        if (keepEp && it.ep > 0) num = it.ep;
        it.newName = BuildNewName(title, num, pad, tag, cm, it.src.extension().wstring());
        it.target = it.src.parent_path() / it.newName;
        n++;
    }

    // 只在同一目录中检查重名；不同子目录可以拥有相同文件名。
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j)
            if (SamePath(items[i].target, items[j].target))
                items[i].dup = items[j].dup = true;

    return true;
}
} // namespace renamer
