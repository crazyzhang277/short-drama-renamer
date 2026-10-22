// 短剧视频批量更名工具 — Win32 原生 GUI（C++17，无第三方依赖）
// 编译见 build.bat
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propvarutil.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cwctype>
#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <io.h>
#include <fcntl.h>

namespace fs = std::filesystem;

// ------------------------------------------------------------------ 控件 ID
enum {
    IDC_EDIT_FOLDER = 1001,
    IDC_BTN_BROWSE,
    IDC_LIST,
    IDC_EDIT_TITLE,
    IDC_EDIT_START,
    IDC_EDIT_PAD,
    IDC_COMBO_SORT,
    IDC_COMBO_TAG,
    IDC_EDIT_CUSTOM,
    IDC_COMBO_COLON,
    IDC_BTN_PREVIEW,
    IDC_BTN_RENAME,
    IDC_BTN_UNDO,
    IDC_BTN_TITLES,
    IDC_BTN_OPEN,
    IDC_CHECK_SUB,
    IDC_STATUS,
    IDC_BTN_GUESS,
    IDC_CHECK_AUTO,
    IDC_CHECK_KEEPEP,
    IDC_BTN_HELP,
    IDC_HELP_TEXT,
    IDC_HELP_CLOSE,
    IDC_BTN_DURATION,
};

static const UINT_PTR IDT_REFRESH = 1;   // 自动刷新的防抖定时器

// ------------------------------------------------------------------ 全局
static HINSTANCE g_hInst;
static HWND  g_hMain, g_hFolder, g_hBrowse, g_hList, g_hTitle, g_hStart, g_hPad,
             g_hSort, g_hTag, g_hCustom, g_hColon, g_hPreview, g_hRename,
             g_hUndo, g_hTitles, g_hOpen, g_hSub, g_hStatus, g_hGuess, g_hAuto,
             g_hHelp, g_hKeepEp, g_hDuration;
static HFONT g_fontUI, g_fontTitle, g_fontSub, g_fontBtn;
static HBRUSH g_brBack;
static int   g_dpi = 96;

static inline int S(int v) { return MulDiv(v, g_dpi, 96); }

static const int HEADER_H = 62;
static const int LBL_W    = 92;   // 字段标签区宽度（96dpi 基准）

// ------------------------------------------------------------------ 标签表
struct TagDef { const wchar_t* en; const wchar_t* zh; };
static const TagDef kTags[] = {
    { L"Billionaire boss",    L"亿万富翁老板" },
    { L"CEO romance",         L"首席执行官恋情" },
    { L"True heiress",        L"真正的继承人" },
    { L"Hidden identity",     L"隐藏的身份" },
    { L"forced marriage",     L"契约婚姻" },
    { L"Alpha",               L"阿尔法" },
    { L"Mate",                L"马特" },
    { L"Werewolf",            L"狼人" },
    { L"Divorce revenge",     L"离婚复仇" },
    { L"Secret baby",         L"秘密宝贝" },
    { L"Single mom",          L"单身妈妈" },
    { L"Stepbrother romance", L"继兄浪漫" },
    { L"Forbidden love",      L"禁忌之爱" },
    { L"Mafia romance",       L"黑手党浪漫" },
    { L"Dark romance",        L"黑暗浪漫" },
};
static const int kTagCount = (int)(sizeof(kTags) / sizeof(kTags[0]));

// ------------------------------------------------------------------ 计划
struct PlanItem {
    fs::path     src;
    std::wstring oldName;
    std::wstring newName;
    long         ep = 0;
    int          score = -1;
    bool         dup = false;   // 与别的文件算出了同一个新名
};
static std::vector<PlanItem> g_plan;
static bool g_previewed = false;
static bool g_titleAuto = false;   // 剧名当前是否由「自动识别」填入
static bool g_suppress = false;    // 抑制程序化设置控件引发的自动刷新

// ------------------------------------------------------------------ 基础工具
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string W2U8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring U82W(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring LowerExt(const fs::path& p) {
    std::wstring e = p.extension().wstring();
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

static bool IsVideoExt(const std::wstring& ext) {
    static const wchar_t* exts[] = {
        L".mp4", L".mkv", L".mov", L".avi", L".wmv", L".flv", L".m4v",
        L".ts",  L".webm", L".mpg", L".mpeg", L".rmvb", L".rm", L".3gp",
        L".m2ts", L".vob", L".ogv", L".divx", L".asf", L".f4v"
    };
    for (auto e : exts) if (ext == e) return true;
    return false;
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().wstring();
}

static std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s((size_t)n, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    return s;
}

static void SetStatus(const std::wstring& s) {
    SetWindowTextW(g_hStatus, s.c_str());
    UpdateWindow(g_hStatus);
}

static std::wstring NowStamp() {
    time_t t = time(nullptr);
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    struct tm* lt = localtime(&t);
    if (lt) tmv = *lt;
    wchar_t buf[64] = {0};
    wcsftime(buf, 64, L"%Y%m%d-%H%M%S", &tmv);
    return buf;
}

// ------------------------------------------------------------------ 集数识别
static bool IsBadNumber(const std::wstring& s) {
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

struct EpResult { long ep = 0; int score = -1; };

static EpResult ParseEpisode(const std::wstring& raw) {
    std::wstring name = raw;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);

    EpResult best;

    auto tryPat = [&](const wchar_t* pat, int score, bool filterBad) {
        try {
            std::wregex re(pat, std::regex_constants::icase);
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
        std::wregex re(L"[Ss]\\d{1,2}[\\s._-]*[Ee][Pp]?\\s*(\\d{1,4})", std::regex_constants::icase);
        std::wsmatch m;
        if (std::regex_search(name, m, re) && m.size() > 1) {
            long v = _wtoi(m[1].str().c_str());
            if (v > 0 && best.score < 120) { best.ep = v; best.score = 120; }
        }
    } catch (...) {}

    tryPat(L"第\\s*(\\d{1,4})\\s*[集话話期]", 130, false);
    tryPat(L"(\\d{1,4})\\s*[集话話]",        125, false);
    tryPat(L"(?:^|[^A-Za-z])[Ee][Pp][\\s._-]*(\\d{1,4})", 110, true);
    tryPat(L"[\\[\\(（【]\\s*(\\d{1,4})\\s*[\\]\\)）】]",   80, true);
    tryPat(L"(?:^|[^\\d])(\\d{1,4})\\s*$",                60, true);
    tryPat(L"^\\s*(\\d{1,4})(?:[^\\d]|$)",                50, true);

    return best;
}

// ------------------------------------------------------------------ 自然排序
static int NaturalCompare(const std::wstring& a, const std::wstring& b) {
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
static std::wstring NormalizePunct(const std::wstring& in) {
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

static std::wstring SanitizeForFile(const std::wstring& in) {
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

static std::wstring CollapseSpaces(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool sp = false;
    for (wchar_t c : in) {
        if (c == L' ') { if (!sp) out += L' '; sp = true; }
        else { out += c; sp = false; }
    }
    return Trim(out);
}

static std::wstring ApplyColon(const std::wstring& s, int mode) {
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
static bool HasCJK(const std::wstring& s) {
    for (wchar_t c : s) if (c >= 0x4E00 && c <= 0x9FFF) return true;
    return false;
}

static bool EqualsNoCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// 这些是"容器目录"名字，不是剧名：盘符、系统目录、常用中文归类目录、本工具目录
static bool IsNoiseSegment(const std::wstring& raw) {
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
        L"deepseek-harness", L"dsh-tools", L"autorename",
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
static std::wstring TitleCaseWords(const std::wstring& s) {
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
static std::wstring FolderNameToTitle(const std::wstring& raw) {
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
static std::wstring GuessTitleFromFolder(const std::wstring& folder) {
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

static std::wstring BuildNewName(const std::wstring& titleRaw, long num, int pad,
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

// ------------------------------------------------------------------ 枚举
static void CollectFiles(const fs::path& dir, bool sub, std::vector<fs::path>& out) {
    std::error_code ec;
    if (sub) {
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
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

// ------------------------------------------------------------------ ListView
static void ListAddRow(int idx, const std::wstring& a, const std::wstring& b, const std::wstring& c,
                       bool dup = false) {
    LVITEMW it = {};
    it.mask = LVIF_TEXT;
    it.iItem = idx;
    it.iSubItem = 0;
    it.pszText = (LPWSTR)a.c_str();
    ListView_InsertItem(g_hList, &it);
    ListView_SetItemText(g_hList, idx, 1, (LPWSTR)b.c_str());
    std::wstring cc = dup ? (L"⚠ 重名 " + c) : c;
    ListView_SetItemText(g_hList, idx, 2, (LPWSTR)cc.c_str());
}

static void SetupListColumns() {
    struct { const wchar_t* t; int w; } cols[] = {
        { L"#",        S(58)  },
        { L"原文件名", S(300) },
        { L"新文件名", S(560) },
    };
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = (LPWSTR)cols[i].t;
        c.cx = cols[i].w;
        c.iSubItem = i;
        ListView_InsertColumn(g_hList, i, &c);
    }
}

static void FitListColumns() {
    RECT rc;
    GetClientRect(g_hList, &rc);
    int w1 = S(58), w2 = S(300);
    int w3 = rc.right - w1 - w2 - S(10);
    if (w3 < S(200)) w3 = S(200);
    ListView_SetColumnWidth(g_hList, 0, w1);
    ListView_SetColumnWidth(g_hList, 1, w2);
    ListView_SetColumnWidth(g_hList, 2, w3);
}

// ------------------------------------------------------------------ 表单取值
static int ColonMode() {
    int i = (int)SendMessageW(g_hColon, CB_GETCURSEL, 0, 0);
    return i < 0 ? 0 : i;
}

static std::wstring SelectedTag() {
    int i = (int)SendMessageW(g_hTag, CB_GETCURSEL, 0, 0);
    if (i < 0) return L"";
    if (i < kTagCount) return kTags[i].en;
    return Trim(GetText(g_hCustom));
}

static int SortMode() {
    int i = (int)SendMessageW(g_hSort, CB_GETCURSEL, 0, 0);
    return i < 0 ? 0 : i;
}

// ------------------------------------------------------------------ 生成计划（纯逻辑，不依赖界面）
static bool MakePlan(const std::wstring& folder, const std::wstring& titleRaw,
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

    std::vector<fs::path> files;
    CollectFiles(fs::path(folder), sub, files);
    if (files.empty()) { err = L"该文件夹里没有找到视频文件。"; return false; }

    items.reserve(files.size());
    for (auto& f : files) {
        PlanItem pi;
        pi.src = f;
        pi.oldName = f.filename().wstring();
        EpResult r = ParseEpisode(pi.oldName);
        pi.ep = r.ep;
        pi.score = r.score;
        items.push_back(pi);
    }

    if (mode == 2) {
        std::sort(items.begin(), items.end(), [](const PlanItem& a, const PlanItem& b) {
            std::error_code e1, e2;
            auto ta = fs::last_write_time(a.src, e1);
            auto tb = fs::last_write_time(b.src, e2);
            if (e1 || e2) return NaturalCompare(a.oldName, b.oldName) < 0;
            if (ta != tb) return ta < tb;
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
        n++;
    }

    // 标出重名：两个文件算出了同一个新名（多见于「沿用原集数」时 EP08 与 第8集 撞车）
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j)
            if (_wcsicmp(items[i].newName.c_str(), items[j].newName.c_str()) == 0)
                items[i].dup = items[j].dup = true;

    return true;
}

// ------------------------------------------------------------------ 生成计划（界面）
static bool BuildPlan(std::wstring& err) {
    g_plan.clear();
    ListView_DeleteAllItems(g_hList);
    g_previewed = false;

    std::wstring folder = Trim(GetText(g_hFolder));
    std::wstring title  = Trim(GetText(g_hTitle));
    long start = _wtoi(GetText(g_hStart).c_str());
    int  pad   = _wtoi(GetText(g_hPad).c_str());
    std::wstring tag = SelectedTag();
    int  cm  = ColonMode();
    int  mode = SortMode();
    bool sub = SendMessageW(g_hSub, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool keepEp = SendMessageW(g_hKeepEp, BM_GETCHECK, 0, 0) == BST_CHECKED;

    std::vector<PlanItem> items;
    if (!MakePlan(folder, title, start, pad, tag, cm, mode, sub, keepEp, items, err)) return false;

    g_plan = items;
    int row = 0;
    int dupCount = 0;
    for (auto& it : g_plan) {
        ListAddRow(row, std::to_wstring(row + 1), it.oldName, it.newName, it.dup);
        if (it.dup) dupCount++;
        row++;
    }

    FitListColumns();
    g_previewed = true;

    const wchar_t* modeName = (mode == 0) ? L"自动识别集数"
                            : (mode == 1) ? L"文件名自然排序" : L"修改时间";
    wchar_t buf[512] = {0};
    swprintf(buf, 512, L"共 %d 个视频 · 排序：%ls · 标签：#%ls",
             (int)g_plan.size(), modeName, tag.empty() ? L"(无)" : tag.c_str());
    SetStatus(buf);
    if (dupCount > 0) {
        wchar_t w[512] = {0};
        swprintf(w, 512, L"⚠ %d 个文件算出了同一个新名（标红行），点「执行改名」会被拦下。"
                         L"可以勾掉「沿用原集数」，或先把重复的片子移走。", dupCount);
        SetStatus(w);
    }
    return true;
}

// ------------------------------------------------------------------ 自动刷新
static bool AutoRefreshOn() {
    return g_hAuto && SendMessageW(g_hAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// 用户改了文件夹路径时，若剧名还是自动填的，就跟着更新
static void SyncTitleFromFolder() {
    if (!g_titleAuto) return;
    std::wstring folder = Trim(GetText(g_hFolder));
    std::wstring t = GuessTitleFromFolder(folder);
    if (t.empty()) return;
    if (Trim(GetText(g_hTitle)) == t) return;
    g_suppress = true;
    SetWindowTextW(g_hTitle, t.c_str());
    g_suppress = false;
}

static void DoAutoRefresh() {
    KillTimer(g_hMain, IDT_REFRESH);
    if (g_suppress) return;
    if (!AutoRefreshOn()) return;

    SyncTitleFromFolder();

    std::wstring folder = Trim(GetText(g_hFolder));
    if (folder.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return;

    if (Trim(GetText(g_hTitle)).empty()) {
        ListView_DeleteAllItems(g_hList);
        g_plan.clear();
        g_previewed = false;
        SetStatus(L"请输入剧名，或点「识别剧名」从文件夹名自动填入");
        return;
    }

    std::wstring err;
    if (!BuildPlan(err)) {
        ListView_DeleteAllItems(g_hList);
        g_plan.clear();
        g_previewed = false;
        SetStatus(err);            // 自动刷新只提示，不弹窗打断输入
    }
}

// 改动后延时 200ms 再刷新，避免每敲一个字都重建列表
static void ScheduleRefresh() {
    if (g_suppress) return;
    if (!AutoRefreshOn()) {
        if (g_previewed) SetStatus(L"设置已变更，点「预览」刷新");
        return;
    }
    SetTimer(g_hMain, IDT_REFRESH, 200, nullptr);
}

// ------------------------------------------------------------------ 撤销记录
static bool SaveUndo(const fs::path& folder,
                     const std::vector<std::pair<std::wstring, std::wstring>>& pairs,
                     std::wstring& outPath, std::wstring& err) {
    std::error_code ec;
    fs::path d = folder / L".rename-tool";
    fs::create_directories(d, ec);
    if (ec) { err = L"无法创建 .rename-tool 目录"; return false; }

    fs::path f = d / (std::wstring(L"undo-") + NowStamp() + L".txt");

    std::string body;
    body += "\xEF\xBB\xBF";
    body += "# rename-tool undo v1\n";
    body += "# folder\t" + W2U8(folder.wstring()) + "\n";
    for (auto& p : pairs) body += W2U8(p.first) + "\t" + W2U8(p.second) + "\n";

    std::ofstream ofs(f, std::ios::binary | std::ios::trunc);
    if (!ofs) { err = L"无法写入撤销记录"; return false; }
    ofs.write(body.data(), (std::streamsize)body.size());
    ofs.close();
    outPath = f.wstring();
    return true;
}

// ------------------------------------------------------------------ 执行改名
static bool DoRename() {
    if (g_plan.empty()) return false;

    std::wstring folder = Trim(GetText(g_hFolder));
    fs::path dir(folder);

    // ---- 冲突检查
    std::wstring conflicts;
    int shown = 0;
    for (size_t i = 0; i < g_plan.size(); ++i) {
        if (g_plan[i].oldName == g_plan[i].newName) continue;

        std::error_code ec;
        if (fs::exists(dir / g_plan[i].newName, ec)) {
            conflicts += L"目标已存在：" + g_plan[i].newName + L"\n";
            if (++shown >= 8) break;
        }
        for (size_t j = i + 1; j < g_plan.size(); ++j) {
            if (_wcsicmp(g_plan[i].newName.c_str(), g_plan[j].newName.c_str()) == 0) {
                conflicts += L"新名重复：" + g_plan[i].newName + L"\n";
                if (++shown >= 8) break;
                break;
            }
        }
        if (shown >= 8) break;

        if ((dir / g_plan[i].newName).wstring().size() > 250) {
            conflicts += L"路径过长：" + g_plan[i].newName + L"\n";
            if (++shown >= 8) break;
        }
    }
    if (!conflicts.empty()) {
        if (shown >= 8) conflicts += L"...（仅显示前几条）\n";
        MessageBoxW(g_hMain, (L"检测到冲突，已取消操作：\n\n" + conflicts).c_str(),
                    L"无法改名", MB_ICONERROR);
        return false;
    }

    std::wstring msg = L"即将重命名 " + std::to_wstring(g_plan.size())
                     + L" 个文件。\n\n确定执行？";
    if (MessageBoxW(g_hMain, msg.c_str(), L"确认改名",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return false;

    // ---- 两段式改名
    struct Step { fs::path from, to; };
    std::vector<Step> done;
    int pid = (int)GetCurrentProcessId();
    std::wstring failMsg;

    auto doMove = [&](const fs::path& a, const fs::path& b) -> bool {
        std::error_code ec;
        fs::rename(a, b, ec);
        if (ec) return false;
        done.push_back({ a, b });
        return true;
    };

    // 阶段一：原名 → 临时名
    std::vector<std::pair<size_t, fs::path>> temps;
    bool ok = true;
    for (size_t i = 0; i < g_plan.size() && ok; ++i) {
        if (g_plan[i].oldName == g_plan[i].newName) continue;
        wchar_t tmpn[160] = {0};
        swprintf(tmpn, 160, L".rntmp_%d_%zu%ls", pid, i, g_plan[i].src.extension().c_str());
        fs::path tmp = dir / tmpn;
        fs::path from = dir / g_plan[i].oldName;
        if (!doMove(from, tmp)) {
            failMsg = L"无法重命名：" + g_plan[i].oldName;
            ok = false;
        } else {
            temps.push_back({ i, tmp });
        }
    }

    // 阶段二：临时名 → 目标名
    if (ok) {
        for (auto& t : temps) {
            fs::path target = dir / g_plan[t.first].newName;
            if (!doMove(t.second, target)) {
                failMsg = L"无法重命名为：" + g_plan[t.first].newName;
                ok = false;
                break;
            }
        }
    }

    if (!ok) {
        // 回滚
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::error_code ec;
            if (fs::exists(it->from, ec)) continue;
            fs::rename(it->to, it->from, ec);
        }
        MessageBoxW(g_hMain, (L"改名失败，已尽力回滚。\n\n" + failMsg).c_str(),
                    L"错误", MB_ICONERROR);
        return false;
    }

    // ---- 撤销记录
    std::vector<std::pair<std::wstring, std::wstring>> pairs;
    for (auto& it : g_plan) pairs.push_back({ it.newName, it.oldName });
    std::wstring undoPath, err;
    if (!SaveUndo(dir, pairs, undoPath, err))
        MessageBoxW(g_hMain, (L"改名成功，但撤销记录写入失败：\n" + err).c_str(),
                    L"提示", MB_ICONWARNING);

    // ---- 标题清单（英文冒号版）
    std::wstring titles;
    for (auto& it : g_plan) {
        std::wstring t = it.newName;
        size_t dot = t.find_last_of(L'.');
        if (dot != std::wstring::npos) t = t.substr(0, dot);
        for (auto& c : t) if (c == L'：') c = L':';
        titles += t + L"\r\n";
    }
    {
        std::error_code ec;
        fs::path d2 = dir / L".rename-tool";
        fs::create_directories(d2, ec);
        std::ofstream ofs(d2 / L"titles.txt", std::ios::binary | std::ios::trunc);
        if (ofs) {
            std::string b = "\xEF\xBB\xBF" + W2U8(titles);
            ofs.write(b.data(), (std::streamsize)b.size());
        }
    }

    wchar_t buf[256] = {0};
    swprintf(buf, 256, L"改名完成，共 %d 个文件", (int)g_plan.size());
    SetStatus(buf);

    std::wstring doneMsg =
        L"改名完成！\n\n"
        L"· 撤销记录：.rename-tool\\undo-*.txt\n"
        L"· 标题清单：.rename-tool\\titles.txt\n\n"
        L"如需还原，点「撤销上次」。";
    MessageBoxW(g_hMain, doneMsg.c_str(), L"成功", MB_ICONINFORMATION);

    PostMessageW(g_hMain, WM_COMMAND, IDC_BTN_PREVIEW, 0);
    return true;
}

// ------------------------------------------------------------------ 撤销
static bool DoUndo() {
    std::wstring folder = Trim(GetText(g_hFolder));
    if (folder.empty()) {
        MessageBoxW(g_hMain, L"请先选择文件夹。", L"提示", MB_ICONWARNING);
        return false;
    }
    fs::path d = fs::path(folder) / L".rename-tool";
    std::error_code ec;
    if (!fs::is_directory(d, ec)) {
        MessageBoxW(g_hMain, L"该文件夹里没有撤销记录。", L"提示", MB_ICONWARNING);
        return false;
    }

    fs::path latest;
    fs::file_time_type bestT{};
    bool found = false;
    for (auto& e : fs::directory_iterator(d, ec)) {
        std::error_code e2;
        if (!e.is_regular_file(e2)) continue;
        std::wstring nm = e.path().filename().wstring();
        if (nm.rfind(L"undo-", 0) != 0) continue;
        auto t = fs::last_write_time(e.path(), e2);
        if (!found || t > bestT) { bestT = t; latest = e.path(); found = true; }
    }
    if (!found) {
        MessageBoxW(g_hMain, L"没有找到撤销记录。", L"提示", MB_ICONWARNING);
        return false;
    }

    std::ifstream ifs(latest, std::ios::binary);
    if (!ifs) {
        MessageBoxW(g_hMain, L"无法读取撤销记录。", L"错误", MB_ICONERROR);
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw = raw.substr(3);

    std::wstring w = U82W(raw);
    std::vector<std::pair<std::wstring, std::wstring>> pairs;
    std::wistringstream ws(w);
    std::wstring line;
    while (std::getline(ws, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        pairs.push_back({ line.substr(0, tab), line.substr(tab + 1) });
    }
    if (pairs.empty()) {
        MessageBoxW(g_hMain, L"撤销记录为空。", L"提示", MB_ICONWARNING);
        return false;
    }

    std::wstring q = L"将撤销 " + std::to_wstring(pairs.size())
                   + L" 个文件的重命名，还原为原名。\n\n确定？";
    if (MessageBoxW(g_hMain, q.c_str(), L"确认撤销",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return false;

    int ok = 0, fail = 0;
    for (auto& p : pairs) {
        fs::path from = fs::path(folder) / p.first;
        fs::path to   = fs::path(folder) / p.second;
        std::error_code e1;
        if (!fs::exists(from, e1)) { fail++; continue; }
        std::error_code e2;
        fs::rename(from, to, e2);
        if (e2) fail++; else ok++;
    }
    fs::remove(latest, ec);

    wchar_t buf[256] = {0};
    swprintf(buf, 256, L"撤销完成：成功 %d 个，失败 %d 个", ok, fail);
    SetStatus(buf);
    MessageBoxW(g_hMain, buf, L"撤销", MB_ICONINFORMATION);
    PostMessageW(g_hMain, WM_COMMAND, IDC_BTN_PREVIEW, 0);
    return true;
}

// ------------------------------------------------------------------ 统计总时长
// 时长走系统自带的 Media Foundation，不需要第三方库；系统不认识的格式会读不到
static bool VideoDurationSec(const fs::path& p, double& sec) {
    IMFSourceReader* reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(p.c_str(), nullptr, &reader)) || !reader)
        return false;
    bool ok = false;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                   MF_PD_DURATION, &v)) && v.vt == VT_UI8) {
        sec = (double)v.uhVal.QuadPart / 10000000.0;   // 100ns -> 秒
        ok = sec > 0;
    }
    PropVariantClear(&v);
    reader->Release();
    return ok;
}

static std::wstring FmtDuration(long sec) {
    long h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    wchar_t b[64] = {0};
    if (h > 0)      swprintf(b, 64, L"%ld 小时 %ld 分 %ld 秒", h, m, s);
    else if (m > 0) swprintf(b, 64, L"%ld 分 %ld 秒", m, s);
    else            swprintf(b, 64, L"%ld 秒", s);
    return b;
}

static void DoCountDuration() {
    std::wstring folder = Trim(GetText(g_hFolder));
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) {
        MessageBoxW(g_hMain, L"请先选择视频文件夹。", L"统计总时长", MB_ICONWARNING);
        return;
    }

    bool sub = SendMessageW(g_hSub, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::vector<fs::path> files;
    CollectFiles(fs::path(folder), sub, files);
    if (files.empty()) {
        MessageBoxW(g_hMain, L"该文件夹里没有找到视频文件。", L"统计总时长", MB_ICONWARNING);
        return;
    }

    HCURSOR oldCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    long total = 0;
    int ok = 0;
    std::vector<std::wstring> bad;
    for (size_t i = 0; i < files.size(); ++i) {
        double sec = 0;
        if (VideoDurationSec(files[i], sec)) { total += (long)(sec + 0.5); ok++; }
        else bad.push_back(files[i].filename().wstring());
        if ((i % 8) == 0) {
            wchar_t b[256] = {0};
            swprintf(b, 256, L"统计时长中… %zu / %zu", i + 1, files.size());
            SetStatus(b);
        }
    }
    SetCursor(oldCur);

    std::wstring msg = L"视频数量：" + std::to_wstring(files.size()) + L" 个\n"
                     + L"总时长：  " + FmtDuration(total) + L"\n";
    if (!bad.empty()) {
        msg += L"\n有 " + std::to_wstring(bad.size()) + L" 个文件读不到时长"
               L"（系统没有对应的解码器）：\n";
        for (size_t i = 0; i < bad.size() && i < 8; ++i) msg += L"  " + bad[i] + L"\n";
        if (bad.size() > 8) msg += L"  ……\n";
    }
    if (ok == 0) msg += L"\n一个时长都没读到，这个格式多半不被系统支持。";

    SetStatus(L"共 " + std::to_wstring(files.size()) + L" 个视频，总时长 " + FmtDuration(total)
              + (bad.empty() ? L"" : L"（" + std::to_wstring(bad.size()) + L" 个读不到时长）"));
    MessageBoxW(g_hMain, msg.c_str(), L"统计总时长", MB_ICONINFORMATION);
}

// ------------------------------------------------------------------ 选文件夹
static bool PickFolder(std::wstring& out) {
    IFileOpenDialog* pDlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pDlg));
    if (SUCCEEDED(hr) && pDlg) {
        DWORD opts = 0;
        pDlg->GetOptions(&opts);
        pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pDlg->SetTitle(L"选择视频文件夹");
        bool got = false;
        if (SUCCEEDED(pDlg->Show(g_hMain))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    out = psz;
                    CoTaskMemFree(psz);
                    got = true;
                }
                pItem->Release();
            }
        }
        pDlg->Release();
        return got;
    }

    BROWSEINFOW bi = {};
    bi.hwndOwner = g_hMain;
    bi.lpszTitle = L"选择视频文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH] = {0};
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out = path;
    return ok;
}

// ------------------------------------------------------------------ 剪贴板
static void CopyToClipboard(const std::wstring& s) {
    if (!OpenClipboard(g_hMain)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else GlobalFree(h);
    }
    CloseClipboard();
}

// ------------------------------------------------------------------ 设置
static void SaveSettings() {
    std::string body;
    auto put = [&](const char* k, const std::wstring& v) {
        body += std::string(k) + "=" + W2U8(v) + "\n";
    };
    put("folder", Trim(GetText(g_hFolder)));
    put("title",  Trim(GetText(g_hTitle)));
    put("start",  Trim(GetText(g_hStart)));
    put("pad",    Trim(GetText(g_hPad)));
    put("custom", Trim(GetText(g_hCustom)));

    wchar_t b[16] = {0};
    swprintf(b, 16, L"%d", (int)SendMessageW(g_hSort,  CB_GETCURSEL, 0, 0)); put("sort",  b);
    swprintf(b, 16, L"%d", (int)SendMessageW(g_hTag,   CB_GETCURSEL, 0, 0)); put("tag",   b);
    swprintf(b, 16, L"%d", (int)SendMessageW(g_hColon, CB_GETCURSEL, 0, 0)); put("colon", b);
    put("sub", SendMessageW(g_hSub, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    put("auto", SendMessageW(g_hAuto, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    put("keepep", SendMessageW(g_hKeepEp, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");

    std::ofstream ofs(fs::path(ExeDir() + L"\\settings.ini"), std::ios::binary | std::ios::trunc);
    if (!ofs) return;
    ofs.write(body.data(), (std::streamsize)body.size());
}

static void LoadSettings() {
    std::ifstream ifs(fs::path(ExeDir() + L"\\settings.ini"), std::ios::binary);
    if (!ifs) return;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw = raw.substr(3);

    std::wstring w = U82W(raw);
    std::wistringstream ws(w);
    std::wstring line;
    while (std::getline(ws, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == L"folder")      SetWindowTextW(g_hFolder, v.c_str());
        else if (k == L"title")  SetWindowTextW(g_hTitle, v.c_str());
        else if (k == L"start")  SetWindowTextW(g_hStart, v.c_str());
        else if (k == L"pad")    SetWindowTextW(g_hPad, v.c_str());
        else if (k == L"custom") SetWindowTextW(g_hCustom, v.c_str());
        else if (k == L"sort")   SendMessageW(g_hSort,  CB_SETCURSEL, _wtoi(v.c_str()), 0);
        else if (k == L"tag")    SendMessageW(g_hTag,   CB_SETCURSEL, _wtoi(v.c_str()), 0);
        else if (k == L"colon")  SendMessageW(g_hColon, CB_SETCURSEL, _wtoi(v.c_str()), 0);
        else if (k == L"sub")    SendMessageW(g_hSub, BM_SETCHECK, v == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
        else if (k == L"auto")   SendMessageW(g_hAuto, BM_SETCHECK, v == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
        else if (k == L"keepep") SendMessageW(g_hKeepEp, BM_SETCHECK, v == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

// ------------------------------------------------------------------ 布局
static void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int m = S(16);
    int y = S(HEADER_H) + S(14);
    int bx = m + S(LBL_W);

    // 行1 文件夹
    MoveWindow(g_hFolder, bx, y, W - bx - m - S(96), S(28), TRUE);
    MoveWindow(g_hBrowse, W - m - S(88), y, S(88), S(28), TRUE);
    y += S(38);

    // 行2 剧名 / 识别剧名 / 起始 / 补零 / 子目录
    int titleW = S(250);
    MoveWindow(g_hTitle, bx, y, titleW, S(28), TRUE);
    int xg = bx + titleW + S(8);
    MoveWindow(g_hGuess, xg, y, S(84), S(28), TRUE);
    int x2 = xg + S(84) + S(16) + S(LBL_W);
    MoveWindow(g_hStart, x2, y, S(64), S(28), TRUE);
    int x3 = x2 + S(64) + S(16) + S(LBL_W);
    MoveWindow(g_hPad, x3, y, S(56), S(28), TRUE);
    int x4 = x3 + S(56) + S(20);
    MoveWindow(g_hSub, x4, y + S(4), S(150), S(22), TRUE);
    y += S(38);

    // 行3 排序 / 标签 / 冒号
    int sortW = S(190);
    MoveWindow(g_hSort, bx, y, sortW, S(220), TRUE);
    int xa = bx + sortW + S(16) + S(LBL_W);
    MoveWindow(g_hTag, xa, y, S(230), S(280), TRUE);
    int xb = xa + S(230) + S(16) + S(LBL_W);
    MoveWindow(g_hColon, xb, y, S(130), S(200), TRUE);
    y += S(38);

    // 行4 自定义标签 / 自动刷新 / 沿用原集数 / 使用教程
    MoveWindow(g_hCustom, bx, y, S(320), S(28), TRUE);
    int xc = bx + S(320) + S(20);
    MoveWindow(g_hAuto,   xc, y + S(4), S(112), S(22), TRUE);
    int xd = xc + S(112) + S(16);
    MoveWindow(g_hKeepEp, xd, y + S(4), S(180), S(22), TRUE);
    int xe = xd + S(180) + S(16);
    MoveWindow(g_hHelp,   xe, y, S(96), S(28), TRUE);
    y += S(38);

    // 列表
    int btnH = S(38);
    int listTop = y;
    int listH = H - listTop - btnH - S(50);
    if (listH < S(140)) listH = S(140);
    MoveWindow(g_hList, m, listTop, W - m * 2, listH, TRUE);
    FitListColumns();

    // 按钮行
    int by = listTop + listH + S(12);
    int bw = S(112), gap = S(10), x = m;
    MoveWindow(g_hPreview, x, by, bw, btnH, TRUE);            x += bw + gap;
    MoveWindow(g_hRename,  x, by, bw + S(16), btnH, TRUE);    x += bw + S(16) + gap;
    MoveWindow(g_hUndo,    x, by, bw, btnH, TRUE);            x += bw + gap;
    MoveWindow(g_hTitles,  x, by, bw + S(24), btnH, TRUE);    x += bw + S(24) + gap;
    MoveWindow(g_hOpen,    x, by, bw, btnH, TRUE);            x += bw + gap;
    MoveWindow(g_hDuration,x, by, bw, btnH, TRUE);

    MoveWindow(g_hStatus, m, H - S(32), W - m * 2, S(22), TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------------ 绘制
static void DrawHeader(HDC hdc, RECT rc) {
    int W = rc.right;
    RECT hr = { 0, 0, W, S(HEADER_H) };
    HBRUSH br = CreateSolidBrush(RGB(23, 37, 84));
    FillRect(hdc, &hr, br);
    DeleteObject(br);

    RECT acc = { 0, 0, S(5), S(HEADER_H) };
    HBRUSH ba = CreateSolidBrush(RGB(59, 130, 246));
    FillRect(hdc, &acc, ba);
    DeleteObject(ba);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    HFONT old = (HFONT)SelectObject(hdc, g_fontTitle);
    RECT t = { S(22), S(8), W - S(20), S(8) + S(30) };
    DrawTextW(hdc, L"短剧视频批量更名工具", -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, g_fontSub);
    SetTextColor(hdc, RGB(147, 178, 232));
    RECT s = { S(22), S(36), W - S(20), S(36) + S(20) };
    DrawTextW(hdc, L"按集数顺序批量重命名 · 自动追加标签 · 支持一键撤销", -1, &s,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, old);
}

static void DrawLabels(HDC hdc, HWND hwnd) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(71, 85, 105));
    HFONT old = (HFONT)SelectObject(hdc, g_fontUI);

    auto lblAt = [&](HWND c, const wchar_t* text) {
        RECT r;
        GetWindowRect(c, &r);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&r, 2);
        RECT t = { r.left - S(LBL_W) - S(2), r.top, r.left - S(8), r.bottom };
        DrawTextW(hdc, text, -1, &t, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    };
    lblAt(g_hFolder, L"视频文件夹");
    lblAt(g_hTitle,  L"剧名");
    lblAt(g_hStart,  L"起始序号");
    lblAt(g_hPad,    L"补零位数");
    lblAt(g_hSort,   L"排序方式");
    lblAt(g_hTag,    L"类型标签");
    lblAt(g_hColon,  L"冒号处理");
    lblAt(g_hCustom, L"自定义标签");

    SelectObject(hdc, old);
}

// ------------------------------------------------------------------ 使用教程窗口
static const wchar_t* kTutorialText =
L"短剧视频批量更名工具 · 使用教程\r\n"
L"\r\n"
L"一、这个工具做什么\r\n"
L"    把一个文件夹里的短剧视频，按集数顺序批量改名成统一格式：\r\n"
L"    剧名 - 序号 #short drama #mini drama #micro drama #类型标签\r\n"
L"    例：Dignity Reborn - 1 #short drama #mini drama #micro drama #Billionaire boss\r\n"
L"\r\n"
L"二、五步搞定\r\n"
L"    1. 点「浏览...」选中放视频的文件夹（也可以直接把文件夹拖进窗口）。\r\n"
L"    2. 填「剧名」，或点「识别剧名」从文件夹名自动认出来。\r\n"
L"    3. 在「类型标签」里选一个；选「自定义」时在下方输入框自己写。\r\n"
L"    4. 看一眼下方预览列表：左边是原文件名，右边是新文件名。\r\n"
L"    5. 确认没问题，点「执行改名」。\r\n"
L"\r\n"
L"三、各项设置说明\r\n"
L"    视频文件夹：要处理的目录。默认只处理这一层，勾「包含子文件夹」会往下钻。\r\n"
L"    剧名：最终文件名的开头部分。中文标点会自动转成英文标点。\r\n"
L"    起始序号：第一个视频的编号，一般填 1。\r\n"
L"    补零位数：填 2 就是 01、02；填 0 就是 1、2。\r\n"
L"    沿用原集数：默认不勾。不勾时按顺序连续编号，原始集数断档会被接上\r\n"
L"        （手上有 1 2 3 8 9 10 11，会变成 01 02 03 04 05 06 07）。\r\n"
L"        勾上则沿用从文件名里认出的集数，断档保留（8 还是 8，9 还是 9）。\r\n"
L"        认不出集数的文件会退回按顺序编号。\r\n"
L"        注意：勾上后如果两个文件认出同一个集数（例如 EP08.mp4 和 第8集.mp4），\r\n"
L"        预览列表里这两行会标红并加「重名」前缀，点「执行改名」会被拦下。\r\n"
L"        这时把重复的片子移走，或者勾掉「沿用原集数」改回连续编号即可。\r\n"
L"    排序方式：\r\n"
L"        · 自动识别集数 —— 优先从文件名里抠出集数（S01E05、第3集、EP12、[05]、结尾数字都认）。\r\n"
L"          认出的比例够高就按集数排，否则退回文件名自然排序。\r\n"
L"        · 文件名自然排序 —— 纯按名字排，1、2、10 会正确排在 2 后面。\r\n"
L"        · 修改时间 —— 按文件最后修改时间排，适合文件名完全没规律的素材。\r\n"
L"    冒号处理：Windows 文件名不允许英文冒号，默认换成全角「：」。\r\n"
L"        选「短横线 -」或「空格」则换成对应的符号。\r\n"
L"    自定义标签：只在自己写标签时用，不需要带 # 号。\r\n"
L"    改动即时预览：勾上后，改任何设置都会自动刷新预览，不用反复点「预览」。\r\n"
L"    「统计总时长」按钮：按当前的「视频文件夹」和「包含子文件夹」，把里面所有视频的时长加起来。\r\n"
L"        时长靠系统自带的解码器读，flv、rmvb 这类系统不认识的格式会读不到，弹窗里会单独列出来。\r\n"
L"\r\n"
L"四、识别剧名的规则\r\n"
L"    从文件夹路径最里层往外找，跳过这些没意义的名字：\r\n"
L"        盘符（C: D: E:）、Users、Desktop、Documents、Downloads、Videos、\r\n"
L"        视频、短剧、影视、素材、新建文件夹、下载、桌面 ……\r\n"
L"    遇到像剧名的目录名就拿来用；英文会自动首字母大写，下划线和点换成空格。\r\n"
L"    找不到时不会乱填，会弹提示让你手动输入。\r\n"
L"\r\n"
L"五、安全机制\r\n"
L"    · 先预览后执行，改名前能看到每一个结果。\r\n"
L"    · 重名、目标已存在、路径过长、非法字符，都会在预览阶段拦住。\r\n"
L"    · 改名分两步走（先临时名再目标名），中途失败会自动回滚。\r\n"
L"    · 每次改名都在文件夹下留一份 .rename-tool\\undo-时间.txt 记录。\r\n"
L"    · 点「撤销上次」可以完整还原，多次改名就多按几次。\r\n"
L"    · 「复制标题清单」把带英文冒号的完整标题复制到剪贴板，方便贴到别处。\r\n"
L"\r\n"
L"六、常见问题\r\n"
L"    问：改完发现剧名填错了怎么办？\r\n"
L"    答：点「撤销上次」还原，改好剧名重新执行一次。\r\n"
L"    问：视频会不会被移动或删除？\r\n"
L"    答：不会。只改文件名，内容、位置、大小都不动。\r\n"
L"    问：文件夹里混了字幕、封面、txt？\r\n"
L"    答：只处理视频扩展名，其它文件原样不动。\r\n"
L"    问：支持哪些视频格式？\r\n"
L"    答：mp4 mkv mov avi wmv flv m4v ts webm mpg mpeg rmvb rm 3gp m2ts vob ogv divx asf f4v。\r\n"
L"    问：统计总时长里说有几个文件读不到时长？\r\n"
L"    答：那几个格式系统没有解压器（多是 flv、rmvb），弹窗里已经列出来了，改名不受影响。\r\n";

static HWND g_hTutWnd = nullptr;
static HWND g_hTutEdit = nullptr;
static HWND g_hTutBtn = nullptr;

static LRESULT CALLBACK TutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hTutEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kTutorialText,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_HELP_TEXT, g_hInst, nullptr);
        g_hTutBtn = CreateWindowExW(0, L"BUTTON", L"知道了",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_HELP_CLOSE, g_hInst, nullptr);
        SendMessageW(g_hTutEdit, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SendMessageW(g_hTutBtn, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SetFocus(g_hTutEdit);
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int m = S(14), bh = S(30);
        MoveWindow(g_hTutEdit, m, m, rc.right - m * 2, rc.bottom - m * 2 - bh - S(10), TRUE);
        MoveWindow(g_hTutBtn, rc.right - m - S(100),
                   rc.bottom - m - bh, S(100), bh, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_HELP_CLOSE) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_hTutWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowTutorial(HWND owner) {
    if (g_hTutWnd && IsWindow(g_hTutWnd)) {
        SetForegroundWindow(g_hTutWnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TutProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"RenamerTutorialWnd";
        wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);
        registered = true;
    }
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = S(760), h = S(600);
    int x = orc.left + ((orc.right - orc.left) - w) / 2;
    int y = orc.top + ((orc.bottom - orc.top) - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    g_hTutWnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"RenamerTutorialWnd", L"使用教程 · 短剧视频批量更名工具",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
        x, y, w, h, owner, nullptr, g_hInst, nullptr);
    if (g_hTutWnd) {
        ShowWindow(g_hTutWnd, SW_SHOW);
        UpdateWindow(g_hTutWnd);
    }
}

// ------------------------------------------------------------------ 窗口过程
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;
        DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;

        g_hFolder = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
            0,0,0,0, hwnd, (HMENU)IDC_EDIT_FOLDER, g_hInst, nullptr);
        g_hBrowse = CreateWindowExW(0, L"BUTTON", L"浏览...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_BROWSE, g_hInst, nullptr);
        g_hTitle = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
            0,0,0,0, hwnd, (HMENU)IDC_EDIT_TITLE, g_hInst, nullptr);
        g_hStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
            editStyle | ES_NUMBER, 0,0,0,0, hwnd, (HMENU)IDC_EDIT_START, g_hInst, nullptr);
        g_hPad = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"2",
            editStyle | ES_NUMBER, 0,0,0,0, hwnd, (HMENU)IDC_EDIT_PAD, g_hInst, nullptr);
        g_hSub = CreateWindowExW(0, L"BUTTON", L"包含子文件夹",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,0,0,0, hwnd, (HMENU)IDC_CHECK_SUB, g_hInst, nullptr);

        g_hSort = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0,0,0,0, hwnd, (HMENU)IDC_COMBO_SORT, g_hInst, nullptr);
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"自动识别集数");
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"文件名自然排序");
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"修改时间");
        SendMessageW(g_hSort, CB_SETCURSEL, 0, 0);

        g_hTag = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0,0,0,0, hwnd, (HMENU)IDC_COMBO_TAG, g_hInst, nullptr);
        for (int i = 0; i < kTagCount; ++i) {
            std::wstring line = kTags[i].zh;
            line += L"   #";
            line += kTags[i].en;
            SendMessageW(g_hTag, CB_ADDSTRING, 0, (LPARAM)line.c_str());
        }
        SendMessageW(g_hTag, CB_ADDSTRING, 0, (LPARAM)L"自定义（在下方输入）");
        SendMessageW(g_hTag, CB_SETCURSEL, 0, 0);

        g_hColon = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0,0,0,0, hwnd, (HMENU)IDC_COMBO_COLON, g_hInst, nullptr);
        SendMessageW(g_hColon, CB_ADDSTRING, 0, (LPARAM)L"全角 ：");
        SendMessageW(g_hColon, CB_ADDSTRING, 0, (LPARAM)L"短横线 -");
        SendMessageW(g_hColon, CB_ADDSTRING, 0, (LPARAM)L"空格");
        SendMessageW(g_hColon, CB_SETCURSEL, 0, 0);

        g_hCustom = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
            0,0,0,0, hwnd, (HMENU)IDC_EDIT_CUSTOM, g_hInst, nullptr);

        g_hGuess = CreateWindowExW(0, L"BUTTON", L"识别剧名",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_GUESS, g_hInst, nullptr);

        g_hAuto = CreateWindowExW(0, L"BUTTON", L"改动即时预览",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,0,0,0, hwnd, (HMENU)IDC_CHECK_AUTO, g_hInst, nullptr);
        SendMessageW(g_hAuto, BM_SETCHECK, BST_CHECKED, 0);

        g_hKeepEp = CreateWindowExW(0, L"BUTTON", L"沿用原集数（断档不补）",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,0,0,0, hwnd, (HMENU)IDC_CHECK_KEEPEP, g_hInst, nullptr);

        g_hHelp = CreateWindowExW(0, L"BUTTON", L"使用教程",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_HELP, g_hInst, nullptr);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
            0,0,0,0, hwnd, (HMENU)IDC_LIST, g_hInst, nullptr);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
        SetupListColumns();

        g_hPreview = CreateWindowExW(0, L"BUTTON", L"预览",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_PREVIEW, g_hInst, nullptr);
        g_hRename = CreateWindowExW(0, L"BUTTON", L"执行改名",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_RENAME, g_hInst, nullptr);
        g_hUndo = CreateWindowExW(0, L"BUTTON", L"撤销上次",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_UNDO, g_hInst, nullptr);
        g_hTitles = CreateWindowExW(0, L"BUTTON", L"复制标题清单",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_TITLES, g_hInst, nullptr);
        g_hOpen = CreateWindowExW(0, L"BUTTON", L"打开文件夹",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_OPEN, g_hInst, nullptr);
        g_hDuration = CreateWindowExW(0, L"BUTTON", L"统计总时长",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,0,0,0, hwnd, (HMENU)IDC_BTN_DURATION, g_hInst, nullptr);

        g_hStatus = CreateWindowExW(0, L"STATIC", L"就绪 · 把视频文件夹直接拖进窗口即可",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
            0,0,0,0, hwnd, (HMENU)IDC_STATUS, g_hInst, nullptr);

        HWND all[] = { g_hFolder, g_hBrowse, g_hTitle, g_hStart, g_hPad, g_hSub,
                       g_hSort, g_hTag, g_hCustom, g_hColon, g_hList, g_hPreview,
                       g_hRename, g_hUndo, g_hTitles, g_hOpen, g_hGuess, g_hAuto,
                       g_hKeepEp, g_hHelp, g_hDuration };
        for (HWND h : all) SendMessageW(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_fontSub, TRUE);

        DragAcceptFiles(hwnd, TRUE);
        LoadSettings();
        EnableWindow(g_hCustom,
            (int)SendMessageW(g_hTag, CB_GETCURSEL, 0, 0) >= kTagCount);
        return 0;
    }

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) Layout(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh && nh->hwndFrom == g_hList && nh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
                size_t i = (size_t)cd->nmcd.dwItemSpec;
                if (i < g_plan.size() && g_plan[i].dup)
                    cd->clrText = RGB(190, 18, 60);   // 重名行标红
                return CDRF_NEWFONT;
            }
            default:
                break;
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_brBack);
        DrawHeader(hdc, rc);
        DrawLabels(hdc, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND c = (HWND)lp;
        SetBkMode(hdc, TRANSPARENT);
        if (c == g_hStatus) SetTextColor(hdc, RGB(100, 116, 139));
        else if (c == g_hSub) SetTextColor(hdc, RGB(51, 65, 85));
        return (LRESULT)g_brBack;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlID == IDC_BTN_RENAME) {
            bool pressed = (di->itemState & ODS_SELECTED) != 0;
            COLORREF bg = pressed ? RGB(29, 78, 216) : RGB(37, 99, 235);
            HBRUSH br = CreateSolidBrush(bg);
            HBRUSH oldB = (HBRUSH)SelectObject(di->hDC, br);
            HPEN pen = CreatePen(PS_NULL, 0, bg);
            HPEN oldP = (HPEN)SelectObject(di->hDC, pen);
            RoundRect(di->hDC, di->rcItem.left, di->rcItem.top,
                      di->rcItem.right, di->rcItem.bottom, S(8), S(8));
            SelectObject(di->hDC, oldB);
            SelectObject(di->hDC, oldP);
            DeleteObject(br);
            DeleteObject(pen);

            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, RGB(255, 255, 255));
            HFONT oldF = (HFONT)SelectObject(di->hDC, g_fontBtn);
            RECT r = di->rcItem;
            DrawTextW(di->hDC, L"执行改名", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(di->hDC, oldF);
            return TRUE;
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        std::wstring folder;
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH * 2] = {0};
            DragQueryFileW(hd, i, buf, MAX_PATH * 2);
            std::error_code ec;
            if (fs::is_directory(buf, ec)) { folder = buf; break; }
            if (fs::is_regular_file(buf, ec)) folder = fs::path(buf).parent_path().wstring();
        }
        DragFinish(hd);
        if (!folder.empty()) {
            g_suppress = true;
            SetWindowTextW(g_hFolder, folder.c_str());
            g_suppress = false;
            if (Trim(GetText(g_hTitle)).empty() || g_titleAuto) {
                std::wstring t = GuessTitleFromFolder(folder);
                if (!t.empty()) {
                    g_suppress = true;
                    SetWindowTextW(g_hTitle, t.c_str());
                    g_suppress = false;
                    g_titleAuto = true;
                }
            }
            DoAutoRefresh();
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        switch (id) {
        case IDC_BTN_BROWSE: {
            std::wstring f;
            if (PickFolder(f)) {
                g_suppress = true;
                SetWindowTextW(g_hFolder, f.c_str());
                g_suppress = false;
                // 剧名还空着（或本来就是自动填的），就顺手从路径里认一下
                if (Trim(GetText(g_hTitle)).empty() || g_titleAuto) {
                    std::wstring t = GuessTitleFromFolder(f);
                    if (!t.empty()) {
                        g_suppress = true;
                        SetWindowTextW(g_hTitle, t.c_str());
                        g_suppress = false;
                        g_titleAuto = true;
                    }
                }
                DoAutoRefresh();
            }
            return 0;
        }
        case IDC_BTN_PREVIEW: {
            std::wstring err;
            if (!BuildPlan(err)) {
                ListView_DeleteAllItems(g_hList);
                g_plan.clear();
                g_previewed = false;
                SetStatus(L"未生成预览：" + err);
                MessageBoxW(hwnd, err.c_str(), L"提示", MB_ICONWARNING);
            }
            return 0;
        }
        case IDC_BTN_RENAME: {
            if (g_plan.empty() || !g_previewed) {
                std::wstring err;
                if (!BuildPlan(err)) {
                    MessageBoxW(hwnd, err.c_str(), L"提示", MB_ICONWARNING);
                    return 0;
                }
            }
            DoRename();
            return 0;
        }
        case IDC_BTN_UNDO:
            DoUndo();
            return 0;
        case IDC_BTN_TITLES: {
            if (g_plan.empty()) {
                MessageBoxW(hwnd, L"请先点「预览」生成清单。", L"提示", MB_ICONWARNING);
                return 0;
            }
            std::wstring t;
            for (auto& it : g_plan) {
                std::wstring s = it.newName;
                size_t dot = s.find_last_of(L'.');
                if (dot != std::wstring::npos) s = s.substr(0, dot);
                for (auto& c : s) if (c == L'：') c = L':';
                t += s + L"\r\n";
            }
            CopyToClipboard(t);
            SetStatus(L"标题清单已复制到剪贴板（" + std::to_wstring(g_plan.size()) + L" 行）");
            return 0;
        }
        case IDC_BTN_OPEN: {
            std::wstring f = Trim(GetText(g_hFolder));
            if (f.empty()) {
                MessageBoxW(hwnd, L"请先选择文件夹。", L"提示", MB_ICONWARNING);
                return 0;
            }
            ShellExecuteW(hwnd, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case IDC_COMBO_TAG: {
            if (code == CBN_SELCHANGE) {
                int i = (int)SendMessageW(g_hTag, CB_GETCURSEL, 0, 0);
                EnableWindow(g_hCustom, i >= kTagCount);
                if (i >= kTagCount) SetFocus(g_hCustom);
                ScheduleRefresh();
            }
            return 0;
        }
        case IDC_COMBO_SORT:
        case IDC_COMBO_COLON:
        case IDC_EDIT_TITLE:
        case IDC_EDIT_START:
        case IDC_EDIT_PAD:
        case IDC_EDIT_CUSTOM:
        case IDC_CHECK_SUB:
        case IDC_CHECK_KEEPEP: {
            if (code == CBN_SELCHANGE || code == EN_KILLFOCUS || code == BN_CLICKED ||
                code == EN_CHANGE) {
                if (id == IDC_EDIT_TITLE && code == EN_CHANGE && !g_suppress)
                    g_titleAuto = false;          // 用户手改了剧名，就不再自动覆盖
                ScheduleRefresh();
            }
            return 0;
        }
        case IDC_BTN_GUESS: {
            std::wstring folder = Trim(GetText(g_hFolder));
            if (folder.empty()) {
                MessageBoxW(hwnd, L"请先选择视频文件夹。", L"提示", MB_ICONWARNING);
                return 0;
            }
            std::wstring t = GuessTitleFromFolder(folder);
            if (t.empty()) {
                MessageBoxW(hwnd,
                    L"没能从这个路径里看出剧名。\n\n"
                    L"盘符（C: D:）、用户目录、桌面、下载、视频、短剧这类"
                    L"通用目录名都会被跳过。\n"
                    L"请手动输入剧名，或把视频放进一个以剧名命名的文件夹。",
                    L"识别剧名", MB_ICONINFORMATION);
                return 0;
            }
            g_suppress = true;
            SetWindowTextW(g_hTitle, t.c_str());
            g_suppress = false;
            g_titleAuto = true;
            SetStatus(L"已从文件夹名识别剧名：" + t);
            ScheduleRefresh();
            return 0;
        }
        case IDC_CHECK_AUTO: {
            if (code == BN_CLICKED) {
                if (AutoRefreshOn()) DoAutoRefresh();
                else SetStatus(L"已关闭即时预览，改完设置请点「预览」");
            }
            return 0;
        }
        case IDC_BTN_DURATION:
            DoCountDuration();
            return 0;
        case IDC_BTN_HELP:
            ShowTutorial(hwnd);
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wp == IDT_REFRESH) {
            DoAutoRefresh();
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_REFRESH);
        SaveSettings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ 自检（无界面）
static int g_fail = 0;
static std::wstring g_log;

static void LOGW(const wchar_t* fmt, ...) {
    wchar_t b[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(b, 2048, fmt, ap);
    va_end(ap);
    g_log += b;
}

static void CHECK(bool cond, const std::wstring& what) {
    if (cond) {
        LOGW(L"  [OK]   %ls\n", what.c_str());
    } else {
        LOGW(L"  [FAIL] %ls\n", what.c_str());
        g_fail++;
    }
}

static void TouchFile(const fs::path& p) {
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << "x";
}

static int RunSelfTest() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    _setmode(_fileno(stdout), _O_U8TEXT);

    LOGW(L"\n=== 短剧更名工具 自检 ===\n\n");

    // ---- 1. 集数识别
    LOGW(L"[1] 集数识别\n");
    struct { const wchar_t* n; long ep; } cases[] = {
        { L"Show.S01E07.1080p.mkv",            7  },
        { L"第12集.mp4",                        12 },
        { L"EP05.mp4",                          5  },
        { L"某剧 13.mp4",                       13 },
        { L"剧名[08].mp4",                      8  },
        { L"09.mp4",                            9  },
        { L"Movie.2023.1080p.mp4",              0  },
        { L"第一季 720p.mp4",                   0  },
    };
    for (auto& c : cases) {
        EpResult r = ParseEpisode(c.n);
        wchar_t b[256];
        swprintf(b, 256, L"%ls -> ep=%ld (score=%d)", c.n, r.ep, r.score);
        CHECK(r.ep == c.ep, b);
    }

    // ---- 2. 自然排序
    LOGW(L"\n[2] 自然排序\n");
    CHECK(NaturalCompare(L"a2.mp4", L"a10.mp4") < 0, L"a2 < a10");
    CHECK(NaturalCompare(L"EP9.mp4", L"EP10.mp4") < 0, L"EP9 < EP10");
    CHECK(NaturalCompare(L"002.mp4", L"010.mp4") < 0, L"002 < 010");
    CHECK(NaturalCompare(L"x.mp4", L"x.mp4") == 0, L"x == x");

    // ---- 3. 标点规范化 + 文件名
    LOGW(L"\n[3] 标点与文件名\n");
    std::wstring t1 = NormalizePunct(L"别惹我，夫人：她回来了’s");
    CHECK(t1 == L"别惹我,夫人:她回来了's", L"中文标点 -> 英文: " + t1);

    std::wstring n1 = BuildNewName(L"Dignity Reborn", 1, 2, L"Billionaire boss", 0, L".mp4");
    CHECK(n1 == L"Dignity Reborn - 01 #short drama #mini drama #micro drama #Billionaire boss.mp4",
          L"标准命名: " + n1);

    std::wstring n2 = BuildNewName(L"Love: Reborn", 3, 0, L"CEO romance", 0, L".mkv");
    CHECK(n2 == L"Love： Reborn - 3 #short drama #mini drama #micro drama #CEO romance.mkv",
          L"冒号->全角: " + n2);

    std::wstring n3 = BuildNewName(L"Love: Reborn", 3, 0, L"CEO romance", 1, L".mkv");
    CHECK(n3 == L"Love - Reborn - 3 #short drama #mini drama #micro drama #CEO romance.mkv",
          L"冒号->短横线: " + n3);

    std::wstring n4 = BuildNewName(L"Bad/Name<>?", 2, 2, L"Alpha", 0, L".mp4");
    CHECK(n4.find(L'/') == std::wstring::npos && n4.find(L'<') == std::wstring::npos,
          L"非法字符已剔除: " + n4);

    // ---- 4. 端到端：建目录 -> 计划 -> 改名 -> 撤销
    LOGW(L"\n[4] 端到端改名与撤销\n");
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    fs::path work = fs::path(tmp) / (L"rn-selftest-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    const wchar_t* srcNames[] = {
        L"剧集 10.mp4", L"剧集 2.mp4", L"剧集 1.mp4", L"剧集 3.mp4"
    };
    for (auto s : srcNames) TouchFile(work / s);

    std::vector<PlanItem> plan;
    std::wstring err;
    bool made = MakePlan(work.wstring(), L"Reborn Heiress", 1, 2,
                         L"True heiress", 0, 0, false, false, plan, err);
    CHECK(made, L"MakePlan 成功" + (made ? L"" : (L"： " + err)));
    CHECK(plan.size() == 4, L"识别到 4 个视频");
    if (made && plan.size() == 4) {
        CHECK(plan[0].ep == 1 && plan[1].ep == 2 && plan[2].ep == 3 && plan[3].ep == 10,
              L"按集数排序 1,2,3,10");
        CHECK(plan[0].newName == L"Reborn Heiress - 01 #short drama #mini drama #micro drama #True heiress.mp4",
              L"第1个新名: " + plan[0].newName);
        CHECK(plan[3].newName == L"Reborn Heiress - 04 #short drama #mini drama #micro drama #True heiress.mp4",
              L"连续编号把断档接上（10 -> 04）: " + plan[3].newName);
    }

    // 沿用原集数：断档保留，10 还是 10
    std::vector<PlanItem> planKeep;
    bool made2 = MakePlan(work.wstring(), L"Reborn Heiress", 1, 2,
                          L"True heiress", 0, 0, false, true, planKeep, err);
    CHECK(made2 && planKeep.size() == 4, L"沿用原集数模式生成成功");
    if (made2 && planKeep.size() == 4) {
        CHECK(planKeep[0].newName == L"Reborn Heiress - 01 #short drama #mini drama #micro drama #True heiress.mp4",
              L"沿用原集数 第1个: " + planKeep[0].newName);
        CHECK(planKeep[3].newName == L"Reborn Heiress - 10 #short drama #mini drama #micro drama #True heiress.mp4",
              L"沿用原集数保留断档（10 还是 10）: " + planKeep[3].newName);
    }

    // 重名标记：EP08 与 第8集 都认出第 8 集
    fs::path dupDir = fs::path(tmp) / (L"rn-selftest-dup-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(dupDir, ec);
    fs::create_directories(dupDir, ec);
    TouchFile(dupDir / L"EP08.mp4");
    TouchFile(dupDir / L"第8集.mp4");
    TouchFile(dupDir / L"1.mp4");
    std::vector<PlanItem> planDup;
    bool madeD = MakePlan(dupDir.wstring(), L"T", 1, 2, L"Alpha", 0, 0, false, true, planDup, err);
    CHECK(madeD && planDup.size() == 3, L"重名目录生成成功");
    if (madeD && planDup.size() == 3) {
        int dupN = 0;
        for (auto& p : planDup) if (p.dup) dupN++;
        CHECK(dupN == 2, L"沿用原集数时 EP08 与 第8集 被标为重名（2 项）");
    }

    // 执行两段式改名
    if (made) {
        bool ok = true;
        int pid = (int)GetCurrentProcessId();
        std::vector<std::pair<fs::path, fs::path>> done;
        for (size_t i = 0; i < plan.size(); ++i) {
            wchar_t tn[160];
            swprintf(tn, 160, L".rntmp_%d_%zu.mp4", pid, i);
            fs::path from = work / plan[i].oldName;
            fs::path to   = work / tn;
            fs::rename(from, to, ec);
            if (ec) { ok = false; break; }
            done.push_back({ to, work / plan[i].newName });
        }
        if (ok) {
            for (auto& d : done) {
                fs::rename(d.first, d.second, ec);
                if (ec) { ok = false; break; }
            }
        }
        CHECK(ok, L"两段式改名执行成功");

        int found = 0;
        for (auto& e : fs::directory_iterator(work, ec))
            if (e.path().extension() == L".mp4") found++;
        CHECK(found == 4, L"改名后仍有 4 个 mp4");

        // 撤销记录
        std::vector<std::pair<std::wstring, std::wstring>> pairs;
        for (auto& it : plan) pairs.push_back({ it.newName, it.oldName });
        std::wstring up, uerr;
        bool saved = SaveUndo(work, pairs, up, uerr);
        CHECK(saved, L"撤销记录已写入");
        CHECK(saved && fs::exists(up), L"撤销文件存在");

        // 还原
        int restored = 0;
        for (auto& p : pairs) {
            fs::path a = work / p.first, b = work / p.second;
            if (!fs::exists(a, ec)) continue;
            fs::rename(a, b, ec);
            if (!ec) restored++;
        }
        CHECK(restored == 4, L"撤销还原 4 个文件");
        CHECK(fs::exists(work / L"剧集 10.mp4", ec), L"原名 剧集 10.mp4 已恢复");
    }

    // ---- 5. 子目录 + 非视频过滤
    LOGW(L"\n[5] 子目录与扩展名过滤\n");
    fs::create_directories(work / L"sub", ec);
    TouchFile(work / L"sub" / L"剧集 4.mp4");
    TouchFile(work / L"readme.txt");
    std::vector<PlanItem> p2;
    MakePlan(work.wstring(), L"T", 1, 2, L"Alpha", 0, 0, false, false, p2, err);
    CHECK(p2.size() == 4, L"不递归时仍为 4（txt 被过滤）");
    std::vector<PlanItem> p3;
    MakePlan(work.wstring(), L"T", 1, 2, L"Alpha", 0, 0, true, false, p3, err);
    CHECK(p3.size() == 5, L"递归时包含子目录，共 5");

    fs::remove_all(work, ec);

    // ---- 6. 从文件夹名识别剧名
    LOGW(L"\n[6] 从文件夹名识别剧名\n");
    CHECK(GuessTitleFromFolder(L"C:\\Users\\EDY\\Desktop\\dignity reborn") ==
              L"Dignity Reborn",
          L"跳过盘符/Users/Desktop -> Dignity Reborn");
    CHECK(GuessTitleFromFolder(L"D:\\短剧\\别惹我，夫人") == L"别惹我，夫人",
          L"跳过 D: 和 短剧 -> 别惹我，夫人");
    CHECK(GuessTitleFromFolder(L"E:\\the_lost-HEIRESS.of time") ==
              L"The Lost Heiress Of Time",
          L"下划线/连字符/点转空格并首字母大写");
    CHECK(GuessTitleFromFolder(L"C:\\Users\\EDY\\Videos\\视频\\下载") == L"",
          L"全是通用目录名时返回空");
    CHECK(GuessTitleFromFolder(L"C:\\") == L"", L"只有盘符时返回空");
    CHECK(GuessTitleFromFolder(L"D:\\新建文件夹\\1") == L"",
          L"新建文件夹和纯数字都被跳过");
    CHECK(GuessTitleFromFolder(L"C:\\CEO revenge") == L"CEO Revenge",
          L"短全大写词 CEO 保留原样");
    CHECK(IsNoiseSegment(L"c:") && IsNoiseSegment(L"Program Files") &&
              IsNoiseSegment(L"待更名"),
          L"IsNoiseSegment 识别盘符/系统目录/中文通用目录");

    // ---- 7. 时长格式化
    LOGW(L"\n[7] 时长格式化\n");
    CHECK(FmtDuration(3725) == L"1 小时 2 分 5 秒", L"3725 秒 -> " + FmtDuration(3725));
    CHECK(FmtDuration(90) == L"1 分 30 秒", L"90 秒 -> " + FmtDuration(90));
    CHECK(FmtDuration(0) == L"0 秒", L"0 秒 -> " + FmtDuration(0));

    LOGW(L"\n=== 结果：%ls（失败 %d 项）===\n",
        g_fail == 0 ? L"全部通过" : L"存在失败", g_fail);

    {
        std::string u8 = W2U8(g_log);
        std::ofstream ofs(fs::path(ExeDir() + L"\\selftest.log"), std::ios::binary | std::ios::trunc);
        if (ofs) ofs.write(u8.data(), (std::streamsize)u8.size());
    }
    if (f) fclose(f);
    FreeConsole();
    return g_fail == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ 入口
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g_hInst = hInst;

    if (wcsstr(GetCommandLineW(), L"--selftest")) {
        return RunSelfTest();
    }

    SetProcessDPIAware();

    HDC hdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    if (g_dpi <= 0) g_dpi = 96;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);      // 统计时长要用

    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    auto mkFont = [](int pt, int weight) {
        int h = -MulDiv(pt, g_dpi, 72);
        return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    };
    g_fontUI    = mkFont(10, FW_NORMAL);
    g_fontTitle = mkFont(15, FW_BOLD);
    g_fontSub   = mkFont(9,  FW_NORMAL);
    g_fontBtn   = mkFont(10, FW_SEMIBOLD);

    g_brBack = CreateSolidBrush(RGB(248, 250, 252));

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"ShortDramaRenamer";
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    int W = MulDiv(1060, g_dpi, 96);
    int H = MulDiv(740, g_dpi, 96);
    int sx = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;

    HWND hwnd = CreateWindowExW(0, L"ShortDramaRenamer", L"短剧视频批量更名工具",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        sx < 0 ? 0 : sx, sy < 0 ? 0 : sy, W, H,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    MFShutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
