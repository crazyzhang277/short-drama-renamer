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
#include <cwctype>
#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <io.h>
#include <fcntl.h>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
#include "renamer_core.h"
#include "renamer_media.h"
#include "renamer_ops.h"

namespace fs = std::filesystem;
using namespace renamer;

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
    // 剧集统计页
    IDC_ST_FOLDER1,
    IDC_ST_BROWSE1,
    IDC_ST_GUESS1,
    IDC_ST_FOLDER2,
    IDC_ST_BROWSE2,
    IDC_ST_GUESS2,
    IDC_ST_TITLE1,
    IDC_ST_EP1,
    IDC_ST_DUR1,
    IDC_ST_TITLE2,
    IDC_ST_EP2,
    IDC_ST_DUR2,
    IDC_ST_SPECIAL,
    IDC_ST_BTN_SCAN,
    IDC_ST_BTN_COPY,
    IDC_ST_REPORT,
};

static const UINT_PTR IDT_REFRESH = 1;   // 自动刷新的防抖定时器
static const UINT WM_MEDIA_PROGRESS = WM_APP + 1;
static const UINT WM_MEDIA_COMPLETE = WM_APP + 2;

// ------------------------------------------------------------------ 全局
static HINSTANCE g_hInst;
static HWND  g_hMain, g_hFolder, g_hBrowse, g_hList, g_hTitle, g_hStart, g_hPad,
             g_hSort, g_hTag, g_hCustom, g_hColon, g_hPreview, g_hRename,
             g_hUndo, g_hTitles, g_hOpen, g_hSub, g_hStatus, g_hGuess, g_hAuto,
             g_hHelp, g_hKeepEp, g_hDuration;
// 剧集统计页控件
static HWND  g_stF1, g_stB1, g_stG1, g_stF2, g_stB2, g_stG2, g_stT1, g_stEp1, g_stDur1,
             g_stT2, g_stEp2, g_stDur2, g_stSpecial, g_stScan, g_stCopy, g_stReport;
static bool g_stT1Auto = false, g_stT2Auto = false;
static int   g_page = 0;          // 0 = 批量改名，1 = 剧集统计
static HFONT g_fontUI, g_fontTitle, g_fontSub, g_fontBtn;
static HBRUSH g_brBack;
static int   g_dpi = 96;

static inline int S(int v) { return MulDiv(v, g_dpi, 96); }

static const int HEADER_H = 62;
static const int TAB_H    = 40;   // 顶部功能页签条
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

static std::vector<PlanItem> g_plan;
static bool g_previewed = false;
static bool g_titleAuto = false;   // 剧名当前是否由「自动识别」填入
static bool g_suppress = false;    // 抑制程序化设置控件引发的自动刷新

enum class MediaTaskKind { Duration, DramaStats };
struct MediaTaskResult {
    MediaTaskKind kind = MediaTaskKind::Duration;
    std::wstring error;
    std::wstring error2;
    bool ok1 = false;
    bool ok2 = false;
    bool sameFolders = false;
    bool cancelled = false;
    DramaScan scan1;
    DramaScan scan2;
    int fileCount = 0;
    int readableCount = 0;
    long totalSeconds = 0;
    std::vector<std::wstring> unreadableFiles;
};
struct MediaProgressMessage {
    size_t current = 0;
    size_t total = 0;
    std::wstring label;
};

static std::atomic<bool> g_mediaCancel{false};
static std::thread g_mediaThread;
static bool g_mediaTaskActive = false;
static bool g_closeAfterMediaTask = false;
static MediaTaskKind g_mediaTaskKind = MediaTaskKind::Duration;
static std::vector<std::pair<HWND, BOOL>> g_mediaControlStates;

// ------------------------------------------------------------------ 基础工具
static std::wstring ExeDir() {
    std::vector<wchar_t> buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    if (n == 0 || n >= buf.size()) return fs::current_path().wstring();
    return fs::path(buf.data()).parent_path().wstring();
}

static std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s((size_t)n + 1, L'\0');
    int copied = GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)copied);
    return s;
}

static void SetStatus(const std::wstring& s) {
    SetWindowTextW(g_hStatus, s.c_str());
    UpdateWindow(g_hStatus);
}

static void SetMediaControlsBusy(bool busy, MediaTaskKind kind) {
    if (busy) {
        g_mediaControlStates.clear();
        std::vector<HWND> controls = { g_hFolder, g_hBrowse, g_hSub, g_hPreview,
                                       g_hRename, g_hUndo, g_hTitles, g_hOpen,
                                       g_hDuration, g_stScan };
        if (kind == MediaTaskKind::DramaStats) {
            controls.insert(controls.end(), { g_stF1, g_stB1, g_stG1, g_stF2, g_stB2, g_stG2, g_stT1,
                                              g_stEp1, g_stDur1, g_stT2, g_stEp2, g_stDur2,
                                              g_stSpecial, g_stCopy });
        }
        for (HWND control : controls) {
            if (!control || !IsWindow(control)) continue;
            g_mediaControlStates.push_back({ control, IsWindowEnabled(control) });
            EnableWindow(control, FALSE);
        }
    } else {
        for (const auto& state : g_mediaControlStates)
            if (state.first && IsWindow(state.first)) EnableWindow(state.first, state.second);
        g_mediaControlStates.clear();
    }
}

static bool StartMediaTask(MediaTaskKind kind,
                           std::function<void(MediaTaskResult&, const std::atomic<bool>&)> work) {
    if (g_mediaTaskActive) return false;
    g_mediaCancel.store(false);
    g_mediaTaskActive = true;
    g_closeAfterMediaTask = false;
    g_mediaTaskKind = kind;
    SetMediaControlsBusy(true, kind);
    SetStatus(kind == MediaTaskKind::Duration ? L"正在后台统计视频时长…" : L"正在后台统计剧集信息…");
    try {
        g_mediaThread = std::thread([kind, work = std::move(work)]() mutable {
            auto result = std::make_unique<MediaTaskResult>();
            result->kind = kind;
            HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(co)) {
                result->error = L"后台线程初始化 COM 失败（系统错误 " +
                                std::to_wstring((unsigned long)co) + L"）。";
                result->error2 = result->error;
            } else {
                HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_FULL);
                if (FAILED(mf)) {
                    result->error = L"后台线程初始化 Media Foundation 失败（系统错误 " +
                                    std::to_wstring((unsigned long)mf) + L"）。";
                    result->error2 = result->error;
                } else {
                    try {
                        work(*result, g_mediaCancel);
                    } catch (...) {
                        result->error = L"统计过程中发生异常。";
                    }
                    MFShutdown();
                }
                CoUninitialize();
            }
            result->cancelled = g_mediaCancel.load();
            if (!PostMessageW(g_hMain, WM_MEDIA_COMPLETE, 0, (LPARAM)result.get())) return;
            result.release();
        });
    } catch (...) {
        g_mediaTaskActive = false;
        SetMediaControlsBusy(false, kind);
        SetStatus(L"无法启动后台统计线程。");
        return false;
    }
    return true;
}

static void PostMediaProgress(size_t current, size_t total, const wchar_t* label = L"") {
    auto* progress = new MediaProgressMessage{ current, total, label };
    if (!PostMessageW(g_hMain, WM_MEDIA_PROGRESS, 0, (LPARAM)progress)) delete progress;
}

// ------------------------------------------------------------------ 集数识别
// ------------------------------------------------------------------ 自然排序
// ------------------------------------------------------------------ 剧名清洗
// ------------------------------------------------------------------ 从文件夹名猜剧名
// 这些是"容器目录"名字，不是剧名：盘符、系统目录、常用中文归类目录、本工具目录
// 单词首字母大写；全大写且很短的词（CEO / TV / FBI）保持原样
// "the_lost-HEIRESS.of time" -> "The Lost Heiress Of Time"
// 从文件夹路径里由深到浅找第一个"像剧名"的目录名；盘符和系统目录一律跳过
// ------------------------------------------------------------------ 枚举
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
        ListAddRow(row, std::to_wstring(row + 1), it.displayName, it.newName, it.dup);
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
    std::wstring t = GuessDramaNamesFromFolder(folder).title;
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

static std::wstring MoveFailReason(DWORD e) {
    switch (e) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION: return L"文件正被其他程序占用";
    case ERROR_ACCESS_DENIED: return L"没有权限";
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return L"文件已经不在了";
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: return L"目标文件已存在";
    case ERROR_FILENAME_EXCED_RANGE: return L"路径太长";
    default: return L"系统错误 " + std::to_wstring(e);
    }
}

// ------------------------------------------------------------------ 执行改名
static bool DoRename() {
    if (g_plan.empty()) return false;
    std::wstring folder = Trim(GetText(g_hFolder));
    if (folder.empty()) {
        MessageBoxW(g_hMain, L"请先选择文件夹。", L"提示", MB_ICONWARNING);
        return false;
    }
    if (MessageBoxW(g_hMain,
                    (L"即将重命名 " + std::to_wstring(g_plan.size()) + L" 个文件。\n\n确定执行？").c_str(),
                    L"确认改名", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return false;
    }

    OperationResult r = ExecuteRename(fs::path(folder), g_plan);
    if (!r.ok) {
        std::wstring message = r.message.empty() ? L"改名失败。" : r.message;
        if (r.win32Error != ERROR_SUCCESS) {
            message += L"\n\n原因：" + MoveFailReason(r.win32Error) + L"（系统错误 " +
                       std::to_wstring(r.win32Error) + L"）";
        }
        MessageBoxW(g_hMain, message.c_str(),
                    L"无法改名", MB_ICONERROR);
        return false;
    }

    std::wstring titles;
    for (const auto& it : g_plan) {
        std::wstring t = it.newName;
        size_t dot = t.find_last_of(L'.');
        if (dot != std::wstring::npos) t.resize(dot);
        for (auto& c : t) if (c == L'：') c = L':';
        titles += t + L"\r\n";
    }
    std::wstring writeErr;
    if (!WriteUtf8Atomic(fs::path(folder) / L".rename-tool" / L"titles.txt",
                         "\xEF\xBB\xBF" + W2U8(titles), writeErr)) {
        MessageBoxW(g_hMain, (L"改名成功，但标题清单写入失败：\n" + writeErr).c_str(),
                    L"提示", MB_ICONWARNING);
    }

    InvalidateScanCache();
    SetStatus(L"改名完成，共 " + std::to_wstring(r.changed) + L" 个文件");
    std::wstring doneMsg = L"改名完成！\n\n";
    if (!r.logPath.empty()) doneMsg += L"· 撤销记录：" + r.logPath.wstring() + L"\n";
    doneMsg += L"· 标题清单：.rename-tool\\titles.txt\n\n如需还原，点「撤销上次」。";
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
    if (MessageBoxW(g_hMain, L"将撤销最近一次改名并恢复原文件名。\n\n确定？",
                    L"确认撤销", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return false;
    }
    OperationResult r = UndoLatest(fs::path(folder));
    if (!r.ok) {
        std::wstring message = r.message.empty() ? L"撤销失败。" : r.message;
        if (r.win32Error != ERROR_SUCCESS) {
            message += L"\n\n原因：" + MoveFailReason(r.win32Error) + L"（系统错误 " +
                       std::to_wstring(r.win32Error) + L"）";
        }
        MessageBoxW(g_hMain, message.c_str(),
                    L"撤销", MB_ICONERROR);
        return false;
    }
    InvalidateScanCache();
    SetStatus(L"撤销完成：成功 " + std::to_wstring(r.changed) + L" 个");
    MessageBoxW(g_hMain, (L"撤销完成：成功 " + std::to_wstring(r.changed) + L" 个").c_str(),
                L"撤销", MB_ICONINFORMATION);
    PostMessageW(g_hMain, WM_COMMAND, IDC_BTN_PREVIEW, 0);
    return true;
}
// ------------------------------------------------------------------ 统计总时长
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
    const fs::path root(folder);
    if (!StartMediaTask(MediaTaskKind::Duration,
        [root, sub](MediaTaskResult& result, const std::atomic<bool>& cancel) {
            DurationScan scan;
            bool ok = CountVideoDurations(root, sub, scan, cancel,
                [](size_t i, size_t n) { if ((i % 8) == 0 || i == n) PostMediaProgress(i, n); },
                result.error);
            if (!ok) return;
            result.fileCount = scan.fileCount;
            result.readableCount = scan.readableCount;
            result.totalSeconds = scan.total;
            result.unreadableFiles = std::move(scan.unreadableFiles);
        })) {
        return;
    }
}

// ------------------------------------------------------------------ 剧集统计
static std::wstring FmtClock(long sec) {
    if (sec < 0) sec = 0;
    long h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    wchar_t b[32] = {0};
    if (h > 0) swprintf(b, 32, L"%ld:%02ld:%02ld", h, m, s);
    else       swprintf(b, 32, L"%ld:%02ld", m, s);
    return b;
}

// secs[i] 是该集秒数（读不到为 0），eps[i] 是集号
static std::wstring BuildSpecialText(int count, const std::vector<double>& secs,
                                     const std::vector<int>& eps) {
    if (count <= 0) return L"";
    std::wstring list;
    int full = 0, partial = 0;
    double mn = 0, mx = 0;
    for (size_t i = 0; i < secs.size() && (int)i < count; ++i) {
        double d = secs[i];
        if (d <= 0) continue;                       // 读不到时长的文件不参与区间统计
        if (d >= 59.5) {
            full++;
            if (!list.empty()) list += L"、";
            list += std::to_wstring(eps.size() > i && eps[i] > 0 ? eps[i] : (int)i + 1);
        } else {
            if (partial == 0) { mn = mx = d; }
            if (d < mn) mn = d;
            if (d > mx) mx = d;
            partial++;
        }
    }

    std::wstring s = L"剧集共" + std::to_wstring(count) + L"集";
    if (full == 0) {
        if (partial > 0) {
            long lo = (long)mn, hi = (long)std::ceil(mx);
            s += L"，均为" + std::to_wstring(lo) +
                 (lo == hi ? L"秒" : L"-" + std::to_wstring(hi) + L"秒");
        }
        return s;
    }
    if (full == count) return s + L"，均满1分钟";

    s += L"，其中第" + list + L"集满1分钟，其余" + std::to_wstring(count - full)
       + L"集均不满一分钟";
    if (partial > 0) {
        long lo = (long)mn, hi = (long)std::ceil(mx);
        s += L"，为" + std::to_wstring(lo) +
             (lo == hi ? L"秒" : L"-" + std::to_wstring(hi) + L"秒之间");
    }
    return s;
}

// 报送文案：固定格式 + 填进去的变量
static std::wstring ComposeReport(const std::wstring& t1,
                                  const std::wstring& ep1, const std::wstring& dur1,
                                  const std::wstring& t2, const std::wstring& ep2,
                                  const std::wstring& dur2, const std::wstring& special) {
    std::wstring s;
    if (!t1.empty()) s += L"原剧:\r\n" + t1 + L"\r\n";
    if (!ep1.empty())  s += L"集数: " + ep1 + L"\r\n";
    if (!dur1.empty()) s += L"时长: " + dur1 + L"\r\n";
    if (!s.empty())    s += L"\r\n";

    if (!t2.empty()) s += L"海外转译后:\r\n" + t2 + L"\r\n";
    if (!ep2.empty())  s += L"集数: " + ep2 + L"\r\n";
    if (!dur2.empty()) s += L"时长: " + dur2 + L"\r\n";
    if (!special.empty()) s += L"特殊情况: " + special + L"\r\n";

    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
    return s;
}

static void UpdateReport() {
    std::wstring text = ComposeReport(
        Trim(GetText(g_stT1)), Trim(GetText(g_stEp1)), Trim(GetText(g_stDur1)),
        Trim(GetText(g_stT2)), Trim(GetText(g_stEp2)),
        Trim(GetText(g_stDur2)), Trim(GetText(g_stSpecial)));
    SetWindowTextW(g_stReport, text.c_str());
}

static void DoScanStats() {
    std::wstring f1 = Trim(GetText(g_stF1)), f2 = Trim(GetText(g_stF2));
    if (f1.empty() && f2.empty()) {
        MessageBoxW(g_hMain, L"请先选择原剧或海外剧所在的文件夹。", L"剧集统计", MB_ICONWARNING);
        return;
    }
    bool sub = SendMessageW(g_hSub, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool same = !f1.empty() && _wcsicmp(f1.c_str(), f2.c_str()) == 0;

    const fs::path root1(f1), root2(f2);
    StartMediaTask(MediaTaskKind::DramaStats,
        [root1, root2, sub, f1, f2, same](MediaTaskResult& result, const std::atomic<bool>& cancel) {
            result.sameFolders = same;
            if (!f1.empty()) {
                result.ok1 = ScanDrama(root1, sub, result.scan1, cancel,
                    [](size_t i, size_t n) {
                        if ((i % 8) == 0 || i == n) PostMediaProgress(i, n, L"原剧");
                    },
                    result.error);
            }
            if (cancel.load()) return;
            if (!f2.empty()) {
                if (same) {
                    result.ok2 = result.ok1;
                    result.scan2 = result.scan1;
                    result.error2 = result.error;
                } else {
                    result.ok2 = ScanDrama(root2, sub, result.scan2, cancel,
                        [](size_t i, size_t n) {
                            if ((i % 8) == 0 || i == n) PostMediaProgress(i, n, L"海外");
                        },
                        result.error2);
                }
            }
        });
}

static void ApplyDramaScan(const MediaTaskResult& result) {
    if (!result.ok1 && !result.ok2) {
        std::wstring msg = L"没有统计到视频。\n";
        if (!Trim(GetText(g_stF1)).empty()) msg += L"\n原剧：" + result.error;
        if (!Trim(GetText(g_stF2)).empty()) msg += L"\n海外：" + result.error2;
        MessageBoxW(g_hMain, msg.c_str(), L"剧集统计", MB_ICONWARNING);
        return;
    }

    g_suppress = true;
    if (result.ok1) {
        SetWindowTextW(g_stEp1, std::to_wstring(result.scan1.count).c_str());
        SetWindowTextW(g_stDur1, FmtClock(result.scan1.total).c_str());
    }
    if (result.ok2) {
        SetWindowTextW(g_stEp2, std::to_wstring(result.scan2.count).c_str());
        SetWindowTextW(g_stDur2, FmtClock(result.scan2.total).c_str());
    }
    const DramaScan& special = result.ok2 ? result.scan2 : result.scan1;
    if (special.count > 0)
        SetWindowTextW(g_stSpecial, BuildSpecialText(special.count, special.secs, special.eps).c_str());
    g_suppress = false;
    UpdateReport();

    std::wstring status;
    auto append = [&](const wchar_t* name, const DramaScan& scan, bool ok) {
        if (!ok) return;
        if (!status.empty()) status += L"  ｜  ";
        status += std::wstring(name) + L" " + std::to_wstring(scan.count) + L" 集 · " + FmtClock(scan.total);
        if (scan.bad > 0) status += L"（" + std::to_wstring(scan.bad) + L" 个读不到时长）";
    };
    append(L"原剧", result.scan1, result.ok1);
    append(L"海外", result.scan2, result.ok2);
    SetStatus(status.empty() ? L"统计完成" : status);
}

static void CompleteMediaTask(MediaTaskResult* raw) {
    std::unique_ptr<MediaTaskResult> result(raw);
    if (g_mediaThread.joinable()) g_mediaThread.join();
    g_mediaTaskActive = false;
    SetMediaControlsBusy(false, result->kind);
    if (g_closeAfterMediaTask) {
        DestroyWindow(g_hMain);
        return;
    }
    if (result->cancelled) { SetStatus(L"统计已取消"); return; }
    if (!result->error.empty() && result->kind == MediaTaskKind::Duration) {
        MessageBoxW(g_hMain, result->error.c_str(), L"统计总时长", MB_ICONWARNING);
        SetStatus(result->error);
        return;
    }
    if (result->kind == MediaTaskKind::Duration) {
        std::wstring msg = L"视频数量：" + std::to_wstring(result->fileCount) + L" 个\n" +
                           L"总时长：  " + FmtDuration(result->totalSeconds) + L"\n";
        if (!result->unreadableFiles.empty()) {
            msg += L"\n有 " + std::to_wstring(result->unreadableFiles.size()) + L" 个文件读不到时长（系统没有对应的解码器）：\n";
            for (size_t i = 0; i < result->unreadableFiles.size() && i < 8; ++i)
                msg += L"  " + result->unreadableFiles[i] + L"\n";
            if (result->unreadableFiles.size() > 8) msg += L"  ……\n";
        }
        if (result->readableCount == 0) msg += L"\n一个时长都没读到，这个格式多半不被系统支持。";
        SetStatus(L"共 " + std::to_wstring(result->fileCount) + L" 个视频，总时长 " +
                  FmtDuration(result->totalSeconds) +
                  (result->unreadableFiles.empty() ? L"" : L"（" +
                   std::to_wstring(result->unreadableFiles.size()) + L" 个读不到时长）"));
        MessageBoxW(g_hMain, msg.c_str(), L"统计总时长", MB_ICONINFORMATION);
    } else {
        ApplyDramaScan(*result);
    }
}

static bool ApplyStatsNameGuess(bool first, const std::wstring& folder, bool overwrite) {
    DramaNameGuess guess = GuessDramaNamesFromFolder(folder);
    if (guess.title.empty()) return false;

    HWND title = first ? g_stT1 : g_stT2;
    bool titleAuto = first ? g_stT1Auto : g_stT2Auto;
    if (overwrite || Trim(GetText(title)).empty() || titleAuto) {
        SetWindowTextW(title, guess.title.c_str());
        if (first) g_stT1Auto = true;
        else g_stT2Auto = true;
    }
    return true;
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
static fs::path SettingsPath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)) &&
        localAppData) {
        fs::path path = fs::path(localAppData) / L"short-drama-renamer" / L"settings.ini";
        CoTaskMemFree(localAppData);
        return path;
    }
    return fs::path(ExeDir()) / L"settings.ini";
}

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

    std::wstring error;
    WriteUtf8Atomic(SettingsPath(), body, error);
}

static void LoadSettings() {
    fs::path path = SettingsPath();
    std::ifstream ifs(path, std::ios::binary);
    bool migrateLegacy = false;
    if (!ifs) {
        // One-time migration from the old portable, exe-adjacent location.
        path = fs::path(ExeDir()) / L"settings.ini";
        ifs.clear();
        ifs.open(path, std::ios::binary);
        migrateLegacy = (bool)ifs;
    }
    if (!ifs) return;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
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
    if (migrateLegacy) SaveSettings();
}

// ------------------------------------------------------------------ 布局
static void LayoutStats(int W, int H) {
    int m = S(16);
    int bx = m + S(LBL_W);
    int y = S(HEADER_H) + S(TAB_H) + S(14);
    int eh = S(28), rh = S(38);
    int right = W - m;

    const int actionW = S(88), actionGap = S(8);
    MoveWindow(g_stF1, bx, y, right - bx - actionW * 2 - actionGap, eh, TRUE);
    MoveWindow(g_stG1, right - actionW * 2 - actionGap, y, actionW, eh, TRUE);
    MoveWindow(g_stB1, right - actionW, y, actionW, eh, TRUE);
    y += rh;
    MoveWindow(g_stF2, bx, y, right - bx - actionW * 2 - actionGap, eh, TRUE);
    MoveWindow(g_stG2, right - actionW * 2 - actionGap, y, actionW, eh, TRUE);
    MoveWindow(g_stB2, right - actionW, y, actionW, eh, TRUE);
    y += rh;

    // 剧名 / 集数 / 时长三列对齐，剧名栏留宽一些显示括号内容。
    int titleW = S(430), epW = S(56), durW = S(88);
    int xEp  = bx + titleW + S(16) + S(LBL_W);
    int xDur = xEp + epW + S(16) + S(LBL_W);
    MoveWindow(g_stT1, bx, y, titleW, eh, TRUE);
    MoveWindow(g_stEp1, xEp, y, epW, eh, TRUE);
    MoveWindow(g_stDur1, xDur, y, durW, eh, TRUE);
    y += rh;
    MoveWindow(g_stT2, bx, y, titleW, eh, TRUE);
    MoveWindow(g_stEp2, xEp, y, epW, eh, TRUE);
    MoveWindow(g_stDur2, xDur, y, durW, eh, TRUE);
    y += rh;

    MoveWindow(g_stSpecial, bx, y, right - bx, eh, TRUE);
    y += rh + S(6);

    int btnH = S(38);
    MoveWindow(g_stScan, m, y, S(116), btnH, TRUE);
    MoveWindow(g_stCopy, m + S(116) + S(10), y, S(116), btnH, TRUE);
    y += btnH + S(10);

    int bottom = H - S(40);
    if (bottom - y < S(60)) bottom = y + S(60);
    MoveWindow(g_stReport, m, y, W - m * 2, bottom - y, TRUE);

    MoveWindow(g_hStatus, m, H - S(32), W - m * 2, S(22), TRUE);
    InvalidateRect(g_hMain, nullptr, FALSE);
}

static void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (g_page == 1) { LayoutStats(W, H); return; }
    int m = S(16);
    int y = S(HEADER_H) + S(TAB_H) + S(14);
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

// ------------------------------------------------------------------ 顶部页签
static RECT TabRect(int i) {
    int x = S(16) + i * S(124);
    RECT r = { x, S(HEADER_H), x + S(116), S(HEADER_H) + S(TAB_H) };
    return r;
}

static void DrawTabs(HDC hdc, const RECT& rc) {
    RECT bar = { 0, S(HEADER_H), rc.right, S(HEADER_H) + S(TAB_H) };
    HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &bar, wb);
    DeleteObject(wb);
    RECT line = { 0, S(HEADER_H) + S(TAB_H) - 1, rc.right, S(HEADER_H) + S(TAB_H) };
    HBRUSH lb = CreateSolidBrush(RGB(226, 232, 240));
    FillRect(hdc, &line, lb);
    DeleteObject(lb);

    const wchar_t* names[2] = { L"批量改名", L"剧集统计" };
    SetBkMode(hdc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(hdc, g_fontUI);
    for (int i = 0; i < 2; ++i) {
        RECT r = TabRect(i);
        bool on = (g_page == i);
        SetTextColor(hdc, on ? RGB(15, 23, 42) : RGB(100, 116, 139));
        DrawTextW(hdc, names[i], -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        if (on) {
            RECT u = { r.left + S(18), r.bottom - S(3), r.right - S(18), r.bottom };
            HBRUSH ub = CreateSolidBrush(RGB(37, 99, 235));
            FillRect(hdc, &u, ub);
            DeleteObject(ub);
        }
    }
    SelectObject(hdc, old);
}

static void DrawStatsLabels(HDC hdc, HWND hwnd) {
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
    lblAt(g_stF1, L"原剧文件夹");
    lblAt(g_stF2, L"海外文件夹");
    lblAt(g_stT1, L"原剧名");
    lblAt(g_stEp1, L"集数");
    lblAt(g_stDur1, L"时长");
    lblAt(g_stT2, L"海外剧名");
    lblAt(g_stEp2, L"集数");
    lblAt(g_stDur2, L"时长");
    lblAt(g_stSpecial, L"特殊情况");

    RECT br;
    GetWindowRect(g_stCopy, &br);
    MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&br, 2);
    RECT hint = { br.right + S(16), br.top, br.right + S(520), br.bottom };
    SetTextColor(hdc, RGB(100, 116, 139));
    DrawTextW(hdc, L"报送文案格式固定，统计后点「复制文案」直接粘贴", -1, &hint,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    SelectObject(hdc, old);
}

// ------------------------------------------------------------------ 页切换
static void ShowPage(HWND hwnd, int page) {
    g_page = page;
    HWND renameCtl[] = { g_hFolder, g_hBrowse, g_hTitle, g_hStart, g_hPad, g_hSub,
                         g_hSort, g_hTag, g_hCustom, g_hColon, g_hList, g_hPreview,
                         g_hRename, g_hUndo, g_hTitles, g_hOpen, g_hGuess, g_hAuto,
                         g_hKeepEp, g_hHelp, g_hDuration };
    HWND statsCtl[] = { g_stF1, g_stB1, g_stG1, g_stF2, g_stB2, g_stG2, g_stT1, g_stEp1,
                        g_stDur1, g_stT2, g_stEp2, g_stDur2, g_stSpecial, g_stScan,
                        g_stCopy, g_stReport };
    for (HWND h : renameCtl) ShowWindow(h, page == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : statsCtl)  ShowWindow(h, page == 1 ? SW_SHOW : SW_HIDE);

    if (page == 1) {
        // 改名页已经选好文件夹/剧名的，直接带过来当「海外」
        if (Trim(GetText(g_stF2)).empty()) {
            std::wstring f = Trim(GetText(g_hFolder));
            if (!f.empty()) SetWindowTextW(g_stF2, f.c_str());
        }
        if (Trim(GetText(g_stT2)).empty()) {
            std::wstring t = Trim(GetText(g_hTitle));
            if (!t.empty()) SetWindowTextW(g_stT2, t.c_str());
        }
        UpdateReport();
    }
    SetFocus(hwnd);
    Layout(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
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
L"五、剧集统计（生成报送文案）\r\n"
L"    顶部有两个页签：批量改名、剧集统计。点「剧集统计」切过去。\r\n"
L"    原剧文件夹：原版短剧放的目录。\r\n"
L"    海外文件夹：海外转译版放的目录。\r\n"
L"    填好点「统计信息」，两边各扫一遍，自动算出集数和总时长。\r\n"
L"    选文件夹时会按目录名尝试识别剧名；也能点旁边的「智能识别」重新识别。\r\n"
L"    括号中的内容会保留在剧名里；认错了可直接修改剧名，集数、时长也能手改。\r\n"
L"    特殊情况按海外剧的每集时长自动判断：哪几集满 1 分钟、其余几集不满 1 分钟、区间多少秒。\r\n"
L"    下方大框就是报送文案，格式固定（原剧 / 集数 / 时长 / 海外转译后 / 特殊情况），\r\n"
L"    改上面任何一格都会立刻同步。点「复制文案」直接粘给对接的人。\r\n"
L"    两个文件夹填成同一个也行，只扫一次。\r\n"
L"\r\n"
L"六、安全机制\r\n"
L"    · 先预览后执行，改名前能看到每一个结果。\r\n"
L"    · 重名、目标已存在、路径过长、非法字符，都会在预览阶段拦住。\r\n"
L"    · 改名分两步走（先临时名再目标名），中途失败会自动回滚。\r\n"
L"    · 每次改名都在文件夹下留一份 .rename-tool\\undo-时间.txt 记录。\r\n"
L"    · 点「撤销上次」可以完整还原，多次改名就多按几次。\r\n"
L"    · 「复制标题清单」把带英文冒号的完整标题复制到剪贴板，方便贴到别处。\r\n"
L"\r\n"
L"七、常见问题\r\n"
L"    问：改完发现剧名填错了怎么办？\r\n"
L"    答：点「撤销上次」还原，改好剧名重新执行一次。\r\n"
L"    问：视频会不会被移动或删除？\r\n"
L"    答：不会。只改文件名，内容、位置、大小都不动。\r\n"
L"    问：文件夹里混了字幕、封面、txt？\r\n"
L"    答：只处理视频扩展名，其它文件原样不动。\r\n"
L"    问：支持哪些视频格式？\r\n"
L"    答：mp4 mkv mov avi wmv flv m4v ts webm mpg mpeg rmvb rm 3gp m2ts vob ogv divx asf f4v。\r\n"
L"    问：统计总时长里说有几个文件读不到时长？\r\n"
L"    答：那几个格式系统没有解压器（多是 flv、rmvb），弹窗里已经列出来了，改名不受影响。\r\n"
L"    问：执行改名弹窗说「文件正被其他程序占用」？\r\n"
L"    答：那个文件正被播放器、网盘同步或杀毒扫描占用，关掉占用它的程序再点一次即可。\r\n"
L"    已经改过的会自动回滚，不会只改一半。\r\n";

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
    case WM_MEDIA_PROGRESS: {
        std::unique_ptr<MediaProgressMessage> progress((MediaProgressMessage*)lp);
        if (progress && g_mediaTaskActive) {
            std::wstring status = g_mediaTaskKind == MediaTaskKind::Duration
                ? L"统计时长中… " : L"统计剧集信息中… ";
            if (!progress->label.empty()) status += progress->label + L" ";
            status += std::to_wstring(progress->current) + L" / " + std::to_wstring(progress->total);
            SetStatus(status);
        }
        return 0;
    }
    case WM_MEDIA_COMPLETE:
        CompleteMediaTask((MediaTaskResult*)lp);
        return 0;

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

        // ---- 剧集统计页（先不显示，切页时再 ShowWindow）
        DWORD se = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL;
        auto mkEdit = [&](int id, const wchar_t* txt, DWORD style) {
            return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt, style,
                0,0,0,0, hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        };
        auto mkBtn = [&](int id, const wchar_t* txt, DWORD style) {
            return CreateWindowExW(0, L"BUTTON", txt, style,
                0,0,0,0, hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        };
        g_stF1  = mkEdit(IDC_ST_FOLDER1, L"", se);
        g_stB1  = mkBtn(IDC_ST_BROWSE1, L"浏览...", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON);
        g_stG1  = mkBtn(IDC_ST_GUESS1, L"智能识别", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON);
        g_stF2  = mkEdit(IDC_ST_FOLDER2, L"", se);
        g_stB2  = mkBtn(IDC_ST_BROWSE2, L"浏览...", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON);
        g_stG2  = mkBtn(IDC_ST_GUESS2, L"智能识别", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON);
        g_stT1  = mkEdit(IDC_ST_TITLE1, L"", se);
        g_stEp1 = mkEdit(IDC_ST_EP1, L"", se);
        g_stDur1 = mkEdit(IDC_ST_DUR1, L"", se);
        g_stT2  = mkEdit(IDC_ST_TITLE2, L"", se);
        g_stEp2 = mkEdit(IDC_ST_EP2, L"", se);
        g_stDur2 = mkEdit(IDC_ST_DUR2, L"", se);
        g_stSpecial = mkEdit(IDC_ST_SPECIAL, L"", se);
        g_stScan = mkBtn(IDC_ST_BTN_SCAN, L"统计信息", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW);
        g_stCopy = mkBtn(IDC_ST_BTN_COPY, L"复制文案", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON);
        g_stReport = mkEdit(IDC_ST_REPORT, L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);

        HWND all[] = { g_hFolder, g_hBrowse, g_hTitle, g_hStart, g_hPad, g_hSub,
                       g_hSort, g_hTag, g_hCustom, g_hColon, g_hList, g_hPreview,
                       g_hRename, g_hUndo, g_hTitles, g_hOpen, g_hGuess, g_hAuto,
                       g_hKeepEp, g_hHelp, g_hDuration,
                       g_stF1, g_stB1, g_stG1, g_stF2, g_stB2, g_stG2, g_stT1, g_stEp1,
                       g_stDur1, g_stT2, g_stEp2, g_stDur2, g_stSpecial, g_stScan,
                       g_stCopy, g_stReport };
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
        DrawTabs(hdc, rc);
        if (g_page == 0) DrawLabels(hdc, hwnd);
        else             DrawStatsLabels(hdc, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
        for (int i = 0; i < 2; ++i) {
            RECT r = TabRect(i);
            if (PtInRect(&r, pt)) {
                if (g_page != i) ShowPage(hwnd, i);
                return 0;
            }
        }
        break;
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
        if (di->CtlID == IDC_BTN_RENAME || di->CtlID == IDC_ST_BTN_SCAN) {
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
            DrawTextW(di->hDC,
                      di->CtlID == IDC_BTN_RENAME ? L"执行改名" : L"统计信息",
                      -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
                std::wstring t = GuessDramaNamesFromFolder(folder).title;
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
                    std::wstring t = GuessDramaNamesFromFolder(f).title;
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
            std::wstring t = GuessDramaNamesFromFolder(folder).title;
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
        case IDC_ST_BROWSE1:
        case IDC_ST_BROWSE2: {
            std::wstring f;
            if (PickFolder(f)) {
                bool first = (id == IDC_ST_BROWSE1);
                SetWindowTextW(first ? g_stF1 : g_stF2, f.c_str());
                g_suppress = true;
                ApplyStatsNameGuess(first, f, false);
                g_suppress = false;
                UpdateReport();
            }
            return 0;
        }
        case IDC_ST_GUESS1:
        case IDC_ST_GUESS2: {
            bool first = (id == IDC_ST_GUESS1);
            std::wstring folder = Trim(GetText(first ? g_stF1 : g_stF2));
            if (folder.empty()) {
                MessageBoxW(hwnd, L"请先选择对应的文件夹。", L"智能识别剧名", MB_ICONWARNING);
                return 0;
            }
            g_suppress = true;
            bool ok = ApplyStatsNameGuess(first, folder, true);
            g_suppress = false;
            if (!ok) {
                MessageBoxW(hwnd,
                    L"没能从这个路径里识别出剧名。\n\n"
                    L"可以把视频放进以剧名命名的文件夹，或直接手动填写。",
                    L"智能识别剧名", MB_ICONINFORMATION);
                return 0;
            }
            UpdateReport();
            SetStatus(L"已智能识别" + std::wstring(first ? L"原剧名" : L"海外剧名") +
                      L"：" + GetText(first ? g_stT1 : g_stT2));
            return 0;
        }
        case IDC_ST_BTN_SCAN:
            DoScanStats();
            return 0;
        case IDC_ST_BTN_COPY: {
            std::wstring t = GetText(g_stReport);
            if (Trim(t).empty()) {
                MessageBoxW(hwnd, L"还没有可复制的文案，先选文件夹并点「统计信息」。",
                            L"复制文案", MB_ICONWARNING);
                return 0;
            }
            CopyToClipboard(t);
            SetStatus(L"报送文案已复制到剪贴板，可直接粘贴");
            return 0;
        }
        case IDC_ST_TITLE1:
        case IDC_ST_EP1:
        case IDC_ST_DUR1:
        case IDC_ST_TITLE2:
        case IDC_ST_EP2:
        case IDC_ST_DUR2:
        case IDC_ST_SPECIAL:
            if (code == EN_CHANGE && !g_suppress) {
                if (id == IDC_ST_TITLE1) g_stT1Auto = false;
                else if (id == IDC_ST_TITLE2) g_stT2Auto = false;
                UpdateReport();
            }
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

    case WM_CLOSE:
        if (g_mediaTaskActive) {
            g_closeAfterMediaTask = true;
            g_mediaCancel.store(true);
            SetStatus(L"正在取消后台统计，完成后关闭窗口…");
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_REFRESH);
        if (g_mediaTaskActive) {
            g_mediaCancel.store(true);
            if (g_mediaThread.joinable()) g_mediaThread.join();
            g_mediaTaskActive = false;
        }
        {
            MSG pending{};
            while (PeekMessageW(&pending, hwnd, WM_MEDIA_PROGRESS, WM_MEDIA_COMPLETE, PM_REMOVE)) {
                if (pending.message == WM_MEDIA_PROGRESS)
                    delete (MediaProgressMessage*)pending.lParam;
                else
                    delete (MediaTaskResult*)pending.lParam;
            }
        }
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

static bool CreateTestDirectory(const fs::path& path) {
    if (CreateDirectoryW(path.wstring().c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static void RemoveTestTree(const fs::path& path) {
    std::wstring pattern = (path / L"*").wstring();
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            fs::path child = path / data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTestTree(child);
            else {
                SetFileAttributesW(child.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.wstring().c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    SetFileAttributesW(path.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(path.wstring().c_str());
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
    const fs::path tempRoot = ExeDir();
    const std::wstring testId = std::to_wstring(GetCurrentProcessId()) + L"-" +
                                 std::to_wstring(GetTickCount64());
    fs::path work = tempRoot / (L".rn-selftest-" + testId);
    std::error_code ec;
    BOOL createdWork = CreateDirectoryW(work.wstring().c_str(), nullptr);
    DWORD createError = createdWork ? ERROR_SUCCESS : GetLastError();
    CHECK(createdWork != FALSE, L"创建自检工作目录（系统错误 " + std::to_wstring(createError) + L"）");

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
    fs::path dupDir = tempRoot / (L".rn-selftest-dup-" + testId);
    CreateTestDirectory(dupDir);
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

    // 通过生产事务执行改名和撤销，覆盖目标名已预先确定、无覆盖和日志清理。
    if (made) {
        OperationResult renamed = ExecuteRename(work, plan);
        CHECK(renamed.ok && renamed.changed == 4, L"事务改名执行成功");

        int found = 0;
        for (auto& e : fs::directory_iterator(work, ec))
            if (e.path().extension() == L".mp4") found++;
        CHECK(found == 4, L"改名后仍有 4 个 mp4");

        OperationResult undone = UndoLatest(work);
        CHECK(undone.ok && undone.changed == 4, L"事务撤销还原 4 个文件");
        CHECK(fs::exists(work / L"剧集 10.mp4", ec), L"原名 剧集 10.mp4 已恢复");
    }

    // 文件名互换必须能完整提交并撤销。
    fs::path swapDir = tempRoot / (L".rn-selftest-swap-" + testId);
    CreateTestDirectory(swapDir);
    TouchFile(swapDir / L"a.mp4");
    TouchFile(swapDir / L"b.mp4");
    PlanItem a, b;
    a.src = swapDir / L"a.mp4"; a.target = swapDir / L"b.mp4";
    b.src = swapDir / L"b.mp4"; b.target = swapDir / L"a.mp4";
    OperationResult swap = ExecuteRename(swapDir, { a, b });
    CHECK(swap.ok && swap.changed == 2, L"互换文件名可通过临时路径提交");
    OperationResult swapUndo = UndoLatest(swapDir);
    CHECK(swapUndo.ok && fs::exists(swapDir / L"a.mp4", ec) && fs::exists(swapDir / L"b.mp4", ec),
          L"互换操作可撤销");

    // 目标由无需改名的源文件占用时必须在操作前拦截。
    fs::path conflictDir = tempRoot / (L".rn-selftest-conflict-" + testId);
    CreateTestDirectory(conflictDir);
    TouchFile(conflictDir / L"source.mp4");
    TouchFile(conflictDir / L"kept.mp4");
    PlanItem moving, kept;
    moving.src = conflictDir / L"source.mp4"; moving.target = conflictDir / L"kept.mp4";
    kept.src = conflictDir / L"kept.mp4"; kept.target = conflictDir / L"kept.mp4";
    OperationResult blocked = ExecuteRename(conflictDir, { moving, kept });
    CHECK(!blocked.ok && fs::exists(moving.src, ec) && fs::exists(kept.src, ec),
          L"未改名源文件占用目标时拒绝执行且保留两文件");

    // 递归预览和撤销必须保留相对目录位置。
    fs::path recursiveDir = tempRoot / (L".rn-selftest-recursive-" + testId);
    CreateTestDirectory(recursiveDir);
    CreateTestDirectory(recursiveDir / L"nested");
    TouchFile(recursiveDir / L"root 1.mp4");
    TouchFile(recursiveDir / L"nested" / L"root 2.mp4");
    std::vector<PlanItem> recursivePlan;
    bool recursiveMade = MakePlan(recursiveDir.wstring(), L"Recursive", 1, 2,
                                  L"Alpha", 0, 0, true, false, recursivePlan, err);
    OperationResult recursiveRename = recursiveMade ? ExecuteRename(recursiveDir, recursivePlan) : OperationResult();
    bool nestedRenamed = false;
    if (recursiveRename.ok) {
        for (const auto& item : recursivePlan)
            if (item.relativeSrc.parent_path() == L"nested" && fs::exists(item.target, ec)) nestedRenamed = true;
    }
    CHECK(recursiveMade && recursivePlan.size() == 2 && recursiveRename.ok && nestedRenamed,
          L"递归改名保持子目录结构");
    OperationResult recursiveUndo = recursiveRename.ok ? UndoLatest(recursiveDir) : OperationResult();
    CHECK(recursiveUndo.ok && fs::exists(recursiveDir / L"nested" / L"root 2.mp4", ec),
          L"递归改名可撤销并恢复到原目录");
    RemoveTestTree(dupDir);
    RemoveTestTree(swapDir);
    RemoveTestTree(conflictDir);
    RemoveTestTree(recursiveDir);

    // ---- 5. 子目录 + 非视频过滤
    LOGW(L"\n[5] 子目录与扩展名过滤\n");
    CreateTestDirectory(work / L"sub");
    TouchFile(work / L"sub" / L"剧集 4.mp4");
    TouchFile(work / L"readme.txt");
    std::vector<PlanItem> p2;
    MakePlan(work.wstring(), L"T", 1, 2, L"Alpha", 0, 0, false, false, p2, err);
    CHECK(p2.size() == 4, L"不递归时仍为 4（txt 被过滤）");
    std::vector<PlanItem> p3;
    MakePlan(work.wstring(), L"T", 1, 2, L"Alpha", 0, 0, true, false, p3, err);
    CHECK(p3.size() == 5, L"递归时包含子目录，共 5");

    RemoveTestTree(work);

    // ---- 6. 从文件夹名识别剧名
    LOGW(L"\n[6] 从文件夹名识别剧名\n");
    CHECK(GuessTitleFromFolder(L"C:\\Users\\Public\\Desktop\\dignity reborn") ==
              L"Dignity Reborn",
          L"跳过盘符/Users/Desktop -> Dignity Reborn");
    CHECK(GuessTitleFromFolder(L"D:\\短剧\\别惹我，夫人") == L"别惹我，夫人",
          L"跳过 D: 和 短剧 -> 别惹我，夫人");
    CHECK(GuessTitleFromFolder(L"E:\\the_lost-HEIRESS.of time") ==
              L"The Lost Heiress Of Time",
          L"下划线/连字符/点转空格并首字母大写");
    CHECK(GuessTitleFromFolder(L"C:\\Users\\Public\\Videos\\视频\\下载") == L"",
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

    // ---- 8. 改名失败原因
    LOGW(L"\n[8] 改名失败原因\n");
    CHECK(MoveFailReason(ERROR_SHARING_VIOLATION).find(L"占用") != std::wstring::npos,
          L"占用 -> " + MoveFailReason(ERROR_SHARING_VIOLATION));
    CHECK(MoveFailReason(ERROR_ACCESS_DENIED).find(L"权限") != std::wstring::npos,
          L"权限 -> " + MoveFailReason(ERROR_ACCESS_DENIED));
    CHECK(MoveFailReason(1234) == L"系统错误 1234", L"未知码 -> " + MoveFailReason(1234));

    // ---- 9. 剧集统计与报送文案
    LOGW(L"\n[9] 剧集统计与报送文案\n");
    CHECK(FmtClock(2725) == L"45:25", L"2725 秒 -> " + FmtClock(2725));
    CHECK(FmtClock(3725) == L"1:02:05", L"3725 秒 -> " + FmtClock(3725));
    CHECK(FmtClock(0) == L"0:00", L"0 秒 -> " + FmtClock(0));

    std::vector<double> secs(54, 0.0);
    std::vector<int> eps;
    for (int i = 0; i < 54; ++i) {
        eps.push_back(i + 1);
        secs[i] = (i % 2) ? 59.4 : 40.1;
    }
    secs[0] = 61.0; secs[25] = 62.0; secs[47] = 61.5;   // 第 1、26、48 集满 1 分钟
    std::wstring sp = BuildSpecialText(54, secs, eps);
    CHECK(sp == L"剧集共54集，其中第1、26、48集满1分钟，其余51集均不满一分钟，为40-60秒之间",
          L"特殊情况: " + sp);
    CHECK(BuildSpecialText(2, { 61.0, 65.0 }, { 1, 2 }) == L"剧集共2集，均满1分钟",
          L"全部满 1 分钟");
    CHECK(BuildSpecialText(2, { 40.0, 50.0 }, { 1, 2 }) == L"剧集共2集，均为40-50秒",
          L"全部不满 1 分钟");
    CHECK(BuildSpecialText(0, {}, {}) == L"", L"没有剧集时为空");

    std::wstring rep = ComposeReport(
        L"顾总裁的烟火厨娘（dy：顾宅烟火遇真心）", L"54", L"45:25",
        L"8 Bowls of Fried Rice for 800 Million", L"54", L"46:23", sp);
    std::wstring want = L"原剧:\r\n顾总裁的烟火厨娘（dy：顾宅烟火遇真心）\r\n集数: 54\r\n时长: 45:25\r\n\r\n"
                        L"海外转译后:\r\n8 Bowls of Fried Rice for 800 Million\r\n集数: 54\r\n时长: 46:23\r\n"
                        L"特殊情况: " + sp;
    CHECK(rep == want, L"报送文案格式");
    CHECK(ComposeReport(L"", L"", L"", L"", L"", L"", L"") == L"", L"空字段时文案为空");
    CHECK(ComposeReport(L"剧", L"10", L"", L"", L"", L"", L"") == L"原剧:\r\n剧\r\n集数: 10",
          L"只有原剧信息");

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

    CoUninitialize();
    return (int)msg.wParam;
}
