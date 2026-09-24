#include "SettingsDialog.h"
#include "AutoStart.h"
#include "ScriptEditor.h"
#include "TimeRangeEditor.h"
#include "SHA256.h"
#include "ScriptSerializer.h"
#include <shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <objbase.h>
#include <comdef.h>
#include <commdlg.h>
#ifndef OFN_SAVEAS
#define OFN_SAVEAS 0x00000002
#endif
#ifndef OFN_PATHMUSTEXIST
#define OFN_PATHMUSTEXIST 0x00000800
#endif
#include <uxtheme.h>
#include <string>
#include <sstream>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")

extern ScriptEngine    g_scriptEngine;
extern ScheduleManager g_scheduleManager;
extern HWND            g_hwnd;

HWND SettingsDialog::s_hDlg = nullptr;
HWND SettingsDialog::s_hTab = nullptr;
std::vector<HWND> SettingsDialog::s_pageCtrls[3];
AppSettings SettingsDialog::g_settings;
ScriptManager SettingsDialog::g_scriptManager;

static const std::wstring PASSWORD_SALT = L"mhlsahz";

// ======================== 辅助 ========================
static std::wstring GetExeDirectory()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p = path;
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos + 1);
    return p;
}

// ======================== 热键录制 ========================
static WNDPROC g_hotkeyOldProc = nullptr;
static LRESULT CALLBACK HotkeyEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU) return 0;
        wchar_t buf[128] = {};
        if (GetKeyState(VK_CONTROL) & 0x8000) wcscat_s(buf, L"Control+");
        if (GetKeyState(VK_MENU) & 0x8000) wcscat_s(buf, L"Alt+");
        if (GetKeyState(VK_SHIFT) & 0x8000) wcscat_s(buf, L"Shift+");

        if ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9')) {
            wchar_t ch[2] = { (wchar_t)wParam, 0 };
            wcscat_s(buf, ch);
        }
        else {
            wchar_t ch[32];
            swprintf_s(ch, L"VK_%02X", (unsigned int)wParam);
            wcscat_s(buf, ch);
        }
        SetWindowTextW(hWnd, buf);
        return 0;
    }
    return CallWindowProcW(g_hotkeyOldProc, hWnd, msg, wParam, lParam);
}

// ======================== 辅助：创建控件 ========================
static void ShowCtrl(HWND hDlg, int ctrlId, bool show) {
    HWND h = GetDlgItem(hDlg, ctrlId);
    if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
}

static HWND MakeCtrl(HWND parent, const wchar_t* cls, const wchar_t* text,
    DWORD style, int x, int y, int w, int h, int id, int page)
{
    HWND hCtrl = CreateWindowExW(0, cls, text, WS_CHILD | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (page >= 0 && page < 3)
        SettingsDialog::s_pageCtrls[page].push_back(hCtrl);
    return hCtrl;
}

// ======================== 初始化 ========================
void SettingsDialog::OnInit(HWND hDlg)
{
    s_hDlg = hDlg;
    s_hTab = GetDlgItem(hDlg, IDC_TAB);

    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    const wchar_t* titles[] = { L"设置", L"脚本", L"时间段" };
    for (int i = 0; i < 3; i++) {
        tie.pszText = (LPWSTR)titles[i];
        TabCtrl_InsertItem(s_hTab, i, &tie);
    }

    g_scriptManager.LoadAll();

    OnInitSettingsPage(hDlg);
    OnInitScriptsPage(hDlg);
    OnInitTimerPage(hDlg);

    HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    for (int p = 0; p < 3; p++)
        for (HWND h : s_pageCtrls[p])
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);

    ShowPage(hDlg, 0);
}

void SettingsDialog::ShowPage(HWND hDlg, int page)
{
    for (int p = 0; p < 3; p++)
        for (HWND h : s_pageCtrls[p])
            ShowWindow(h, (p == page) ? SW_SHOW : SW_HIDE);
}

void SettingsDialog::OnTabChanged(HWND hDlg, int sel)
{
    ShowPage(hDlg, sel);
}

// ======================== 设置页 ========================
void SettingsDialog::OnInitSettingsPage(HWND hDlg)
{
    const int PX = 15, PY = 30;
    const int W_CTRL = 280;

    MakeCtrl(hDlg, L"BUTTON", L"始终隐藏托盘图标",
        BS_AUTOCHECKBOX, PX, PY, W_CTRL, 20, IDC_CHK_HIDE_TRAY, 0);

    MakeCtrl(hDlg, L"BUTTON", L"日志记录至文件",
        BS_AUTOCHECKBOX, PX, PY + 26, W_CTRL, 20, IDC_CHK_LOG, 0);

    MakeCtrl(hDlg, L"STATIC", L"日志文件位置：", SS_LEFT, PX, PY + 52, 75, 20, -1, 0);
    MakeCtrl(hDlg, L"EDIT", L"D:\\CameraMonitor.log",
        WS_BORDER | ES_AUTOHSCROLL, PX + 78, PY + 50, 145, 22, IDC_EDIT_LOG_PATH, 0);
    MakeCtrl(hDlg, L"BUTTON", L"浏览...",
        BS_PUSHBUTTON, PX + 228, PY + 50, 52, 22, IDC_BTN_BROWSE_LOG, 0);

    MakeCtrl(hDlg, L"STATIC", L"设置面板快捷键：", SS_LEFT, PX, PY + 82, 95, 20, -1, 0);
    MakeCtrl(hDlg, L"EDIT", L"Control+Alt+C",
        WS_BORDER | ES_AUTOHSCROLL, PX + 98, PY + 80, 180, 22, IDC_EDIT_HOTKEY, 0);

    MakeCtrl(hDlg, L"STATIC", L"全局音量调节：", SS_LEFT, PX, PY + 112, 95, 20, -1, 0);
    HWND hSlider = MakeCtrl(hDlg, L"msctls_trackbar32", L"",
        TBS_HORZ | TBS_NOTICKS, PX + 98, PY + 108, 180, 30, IDC_SLIDER_VOLUME, 0);
    SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(hSlider, TBM_SETPOS, TRUE, 50);
    SetWindowTheme(hSlider, L"", L"");

    MakeCtrl(hDlg, L"BUTTON", L"设置密码",
        BS_PUSHBUTTON, PX, PY + 145, 75, 22, IDC_BTN_SET_PWD, 0);

    MakeCtrl(hDlg, L"STATIC", L"配置存储方式：", SS_LEFT, PX, PY + 175, 95, 20, -1, 0);
    MakeCtrl(hDlg, L"BUTTON", L"注册表",
        BS_AUTORADIOBUTTON | WS_GROUP, PX + 98, PY + 173, 60, 20, IDC_RADIO_REG, 0);
    MakeCtrl(hDlg, L"BUTTON", L"exe同目录",
        BS_AUTORADIOBUTTON, PX + 163, PY + 173, 80, 20, IDC_RADIO_DIR, 0);

    MakeCtrl(hDlg, L"BUTTON", L"导出配置",
        BS_PUSHBUTTON, PX, PY + 203, 75, 22, IDC_BTN_EXPORT, 0);
    MakeCtrl(hDlg, L"BUTTON", L"导入配置",
        BS_PUSHBUTTON, PX + 82, PY + 203, 75, 22, IDC_BTN_IMPORT, 0);

    MakeCtrl(hDlg, L"STATIC", L"托盘图标：",
        SS_LEFT, PX, PY + 233, 60, 20, IDC_STATIC_TRAY_ICON, 0);
    MakeCtrl(hDlg, L"BUTTON", L"选择图标",
        BS_PUSHBUTTON, PX + 65, PY + 231, 70, 22, IDC_BTN_TRAY_ICON, 0);
    MakeCtrl(hDlg, L"BUTTON", L"恢复默认",
        BS_PUSHBUTTON, PX + 140, PY + 231, 70, 22, IDC_BTN_TRAY_ICON_RESET, 0);

    CheckDlgButton(hDlg, IDC_CHK_HIDE_TRAY, g_settings.hideTrayIcon ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHK_LOG, g_settings.logToFile ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hDlg, IDC_EDIT_LOG_PATH, g_settings.logPath.c_str());
    SetDlgItemTextW(hDlg, IDC_EDIT_HOTKEY, g_settings.hotkey.c_str());
    SendMessageW(hSlider, TBM_SETPOS, TRUE, g_settings.volume);
    CheckRadioButton(hDlg, IDC_RADIO_REG, IDC_RADIO_DIR,
        g_settings.storage == StorageType::Registry ? IDC_RADIO_REG : IDC_RADIO_DIR);

    HWND hHotkey = GetDlgItem(hDlg, IDC_EDIT_HOTKEY);
    g_hotkeyOldProc = (WNDPROC)SetWindowLongPtrW(hHotkey, GWLP_WNDPROC, (LONG_PTR)HotkeyEditProc);
}

// ======================== 脚本页 ========================
void SettingsDialog::OnInitScriptsPage(HWND hDlg)
{
    const int PX = 15, PY = 30;

    HWND hList = MakeCtrl(hDlg, WC_LISTVIEWW, L"",
        WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        PX, PY, 270, 220, IDC_LIST_SCRIPTS, 1);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = (LPWSTR)L"脚本名称";
    lvc.cx = 140;
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.pszText = (LPWSTR)L"大小";
    lvc.cx = 70;
    ListView_InsertColumn(hList, 1, &lvc);
    lvc.pszText = (LPWSTR)L"状态";
    lvc.cx = 55;
    ListView_InsertColumn(hList, 2, &lvc);

    MakeCtrl(hDlg, L"BUTTON", L"+",
        BS_PUSHBUTTON, PX + 275, PY, 30, 30, IDC_BTN_ADD_SCRIPT, 1);

    LoadScriptsToList(hDlg);
}

void SettingsDialog::LoadScriptsToList(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPTS);
    ListView_DeleteAllItems(hList);

    int count = g_scriptManager.GetCount();
    for (int i = 0; i < count; i++) {
        const ScriptInfo* info = g_scriptManager.GetScript(i);
        if (!info) continue;

        size_t bytes = ScriptManager::GetScriptSize(*info);
        wchar_t sizeBuf[32];
        if (bytes < 1024) swprintf_s(sizeBuf, L"%zu B", bytes);
        else swprintf_s(sizeBuf, L"%.2f KB", bytes / 1024.0);

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.pszText = (LPWSTR)info->name.c_str();
        ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, i, 1, sizeBuf);
        ListView_SetItemText(hList, i, 2, (LPWSTR)(info->enabled ? L"启用" : L"禁用"));
    }
}

// ======================== 时间段页 ========================
void SettingsDialog::OnInitTimerPage(HWND hDlg)
{
    const int PX = 15, PY = 30;

    MakeCtrl(hDlg, L"BUTTON", L"启用此项设置",
        BS_AUTOCHECKBOX, PX, PY, 150, 20, IDC_CHK_ENABLE_TIMER, 2);

    HWND hList = MakeCtrl(hDlg, WC_LISTVIEWW, L"",
        WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        PX, PY + 28, 270, 190, IDC_LIST_TIMERANGES, 2);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = (LPWSTR)L"时间段";
    lvc.cx = 110;
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.pszText = (LPWSTR)L"操作";
    lvc.cx = 70;
    ListView_InsertColumn(hList, 1, &lvc);
    lvc.pszText = (LPWSTR)L"星期";
    lvc.cx = 85;
    ListView_InsertColumn(hList, 2, &lvc);

    MakeCtrl(hDlg, L"BUTTON", L"+",
        BS_PUSHBUTTON, PX + 275, PY + 28, 30, 30, IDC_BTN_ADD_TIMERANGE, 2);

    LoadRangesToList(hDlg);
}

void SettingsDialog::LoadRangesToList(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_TIMERANGES);
    ListView_DeleteAllItems(hList);

    auto ranges = g_scheduleManager.GetRanges();
    for (size_t i = 0; i < ranges.size(); i++) {
        const TimeRange& r = ranges[i];
        std::wstring daysStr;
        const wchar_t* dayShort[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
        for (int d = 0; d < 7; d++)
            if (r.days[d]) daysStr += dayShort[d];
        if (daysStr.empty()) daysStr = L"无";

        wchar_t timeBuf[64];
        swprintf_s(timeBuf, L"%02d:%02d-%02d:%02d",
            r.startHour, r.startMinute, r.endHour, r.endMinute);

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)i;
        lvi.pszText = timeBuf;
        ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, (int)i, 1, (LPWSTR)(r.enabled ? L"启用" : L"禁用"));
        ListView_SetItemText(hList, (int)i, 2, (LPWSTR)daysStr.c_str());
    }
}

// ======================== 密码 ========================
INT_PTR CALLBACK SettingsDialog::PasswordDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        EnumChildWindows(hDlg, [](HWND hChild, LPARAM lp) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
            }, (LPARAM)hFont);
    }
    return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_PWD_OK) {
            wchar_t pwd1[128], pwd2[128];
            GetDlgItemTextW(hDlg, IDC_EDIT_PWD1, pwd1, 128);
            GetDlgItemTextW(hDlg, IDC_EDIT_PWD2, pwd2, 128);
            if (wcscmp(pwd1, pwd2) != 0) {
                MessageBoxW(hDlg, L"两次输入的密码不一致！", L"错误", MB_ICONERROR);
                return TRUE;
            }
            g_settings.password = SHA256Hash(std::wstring(pwd1) + PASSWORD_SALT);
            EndDialog(hDlg, IDOK);
        }
        else if (LOWORD(wParam) == IDC_BTN_PWD_CANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK SettingsDialog::PasswordInputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        EnumChildWindows(hDlg, [](HWND hChild, LPARAM lp) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
            }, (LPARAM)hFont);
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_PWD_INPUT));
    }
    return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t pwd[256] = {};
            GetDlgItemTextW(hDlg, IDC_EDIT_PWD_INPUT, pwd, 256);
            if (SHA256Hash(std::wstring(pwd) + PASSWORD_SALT) == g_settings.password) {
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxW(hDlg, L"密码错误！", L"错误", MB_ICONERROR);
                SetDlgItemTextW(hDlg, IDC_EDIT_PWD_INPUT, L"");
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_PWD_INPUT));
            }
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

bool SettingsDialog::CheckPassword(HWND parent)
{
    if (g_settings.password.empty()) return true;
    INT_PTR ret = DialogBoxParamW(GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_PASSWORD_INPUT), parent, PasswordInputDlgProc, 0);
    return ret == IDOK;
}

// ======================== 配置导入导出 ========================
bool SettingsDialog::ExportConfig(const std::wstring& path)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f)
        return false;

    fwprintf(f, L"%s\n", HEADER_CONFIG);
    fwprintf(f, L"HideTrayIcon=%d\n", g_settings.hideTrayIcon ? 1 : 0);
    fwprintf(f, L"LogToFile=%d\n", g_settings.logToFile ? 1 : 0);
    fwprintf(f, L"LogPath=%s\n", g_settings.logPath.c_str());
    fwprintf(f, L"Hotkey=%s\n", g_settings.hotkey.c_str());
    fwprintf(f, L"Volume=%d\n", g_settings.volume);
    fwprintf(f, L"Storage=%d\n", (int)g_settings.storage);
    fwprintf(f, L"Password=%s\n", g_settings.password.c_str());
    fwprintf(f, L"TrayIconFile=%s\n", g_settings.customTrayIconFile.c_str());

    auto ranges = g_scheduleManager.GetRanges();
    fwprintf(f, L"TimeRangeCount=%zu\n", ranges.size());
    for (size_t i = 0; i < ranges.size(); i++) {
        std::wstring days;
        for (int d = 0; d < 7; d++) days += std::to_wstring(ranges[i].days[d] ? 1 : 0);
        fwprintf(f, L"TimeRange%zu=%d:%d-%d:%d|%s|%d\n",
            i, ranges[i].startHour, ranges[i].startMinute,
            ranges[i].endHour, ranges[i].endMinute,
            days.c_str(), ranges[i].enabled ? 1 : 0);
    }

    fclose(f);
    MessageBoxW(NULL, L"配置导出成功。", L"提示", MB_OK | MB_ICONINFORMATION);
    return true;
}

bool SettingsDialog::ImportConfig(const std::wstring& path)
{
    if (!CheckFileHeader(NULL, path, FileType::Config)) return false;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
        return false;

    std::vector<TimeRange> ranges;
    wchar_t line[2048];
    bool firstLine = true;

    while (fgetws(line, 2048, f)) {
        if (firstLine) { firstLine = false; continue; }
        std::wstring s = line;
        if (!s.empty() && s.back() == L'\n') s.pop_back();
        if (s.empty()) continue;

        if (s.find(L"HideTrayIcon=") == 0)
            g_settings.hideTrayIcon = _wtoi(s.substr(13).c_str()) != 0;
        else if (s.find(L"LogToFile=") == 0)
            g_settings.logToFile = _wtoi(s.substr(10).c_str()) != 0;
        else if (s.find(L"LogPath=") == 0)
            g_settings.logPath = s.substr(8);
        else if (s.find(L"Hotkey=") == 0)
            g_settings.hotkey = s.substr(7);
        else if (s.find(L"Volume=") == 0)
            g_settings.volume = _wtoi(s.substr(7).c_str());
        else if (s.find(L"Storage=") == 0)
            g_settings.storage = (StorageType)_wtoi(s.substr(8).c_str());
        else if (s.find(L"Password=") == 0)
            g_settings.password = s.substr(9);
        else if (s.find(L"TrayIconFile=") == 0)
            g_settings.customTrayIconFile = s.substr(13);
        else if (s.find(L"TimeRange") == 0 && s.find(L'=') != std::wstring::npos) {
            std::wstring val = s.substr(s.find(L'=') + 1);
            size_t p1 = val.find(L'|');
            size_t p2 = val.find(L'|', p1 + 1);
            if (p1 != std::wstring::npos && p2 != std::wstring::npos) {
                TimeRange r;
                swscanf_s(val.substr(0, p1).c_str(), L"%d:%d-%d:%d",
                    &r.startHour, &r.startMinute, &r.endHour, &r.endMinute);
                std::wstring days = val.substr(p1 + 1, p2 - p1 - 1);
                for (int d = 0; d < 7 && d < (int)days.size(); d++)
                    r.days[d] = days[d] == L'1';
                r.enabled = _wtoi(val.substr(p2 + 1).c_str()) != 0;
                ranges.push_back(r);
            }
        }
    }
    fclose(f);

    g_scheduleManager.SetRanges(ranges);
    return true;
}

// ======================== 主对话框 ========================
INT_PTR CALLBACK SettingsDialog::DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        OnInit(hDlg);
        RECT rc;
        GetWindowRect(hDlg, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hDlg, NULL, (sw - w) / 2, (sh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        HICON hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    return TRUE;

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_TAB && pnmh->code == TCN_SELCHANGE) {
            OnTabChanged(hDlg, TabCtrl_GetCurSel(s_hTab));
        }
        if (pnmh->code == NM_RCLICK) {
            SetFocus(pnmh->hwndFrom);
        }
    }
    return TRUE;

    case WM_CONTEXTMENU:
    {
        HWND hFocus = GetFocus();
        if (hFocus == GetDlgItem(hDlg, IDC_LIST_SCRIPTS)) {
            int sel = ListView_GetNextItem(hFocus, -1, LVNI_SELECTED);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_NEW_SCRIPT, L"新建");
            AppendMenuW(hMenu, MF_STRING, IDM_IMPORT_SCRIPT, L"导入");
            if (sel != -1) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_EDIT_SCRIPT, L"编辑");
                AppendMenuW(hMenu, MF_STRING, IDM_DEL_SCRIPT, L"删除");
                AppendMenuW(hMenu, MF_STRING, IDM_EXPORT_SCRIPT, L"导出");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_SCRIPT, L"启用/禁用");
            }
            POINT pt;
            GetCursorPos(&pt);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        }
        else if (hFocus == GetDlgItem(hDlg, IDC_LIST_TIMERANGES)) {
            int sel = ListView_GetNextItem(hFocus, -1, LVNI_SELECTED);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_NEW_RANGE, L"新建");
            AppendMenuW(hMenu, MF_STRING, IDM_IMPORT_RANGE, L"导入");
            if (sel != -1) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_DEL_RANGE, L"删除");
                AppendMenuW(hMenu, MF_STRING, IDM_EXPORT_RANGE, L"导出");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_RANGE, L"启用/禁用");
            }
            POINT pt;
            GetCursorPos(&pt);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        }
    }
    return TRUE;

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_VOLUME);
        if ((HWND)lParam == hSlider) {
            g_settings.volume = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
        }
    }
    return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_BROWSE_LOG:
        {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = {};
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"日志文件\0*.log\0所有文件\0*.*\0";
            ofn.Flags = OFN_SAVEAS | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_EDIT_LOG_PATH, path);
            }
        }
        break;

        case IDC_BTN_SET_PWD:
            DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_PASSWORD), hDlg, PasswordDlgProc, 0);
            break;

        case IDC_BTN_TRAY_ICON:
        {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = {};
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"图标文件\0*.ico\0所有文件\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                // 1. 读源文件
                FILE* fSrc = nullptr;
                if (_wfopen_s(&fSrc, path, L"rb") == 0 && fSrc) {
                    fseek(fSrc, 0, SEEK_END);
                    long size = ftell(fSrc);
                    fseek(fSrc, 0, SEEK_SET);

                    std::vector<BYTE> data((size_t)size);
                    if (size > 0) fread(data.data(), 1, size, fSrc);
                    fclose(fSrc);

                    if (!data.empty() && data.size() <= 200 * 1024) {
                        // 2. 复制到 exe 同目录
                        std::wstring dstPath = GetExeDirectory() + L"custom.ico";
                        FILE* fDst = nullptr;
                        if (_wfopen_s(&fDst, dstPath.c_str(), L"wb") == 0 && fDst) {
                            fwrite(data.data(), 1, data.size(), fDst);
                            fclose(fDst);

                            // 3. 保存文件名
                            g_settings.customTrayIconFile = L"custom.ico";

                            // 4. 立即应用
                            HICON hIcon = (HICON)LoadImageW(NULL,
                                dstPath.c_str(), IMAGE_ICON,
                                0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
                            if (hIcon) {
                                PostMessage(g_hwnd, WM_APP + 201, (WPARAM)hIcon, 0);
                            }
                            else {
                                MessageBoxW(hDlg,
                                    L"图标加载失败，请确认 .ico 文件有效。",
                                    L"错误", MB_ICONERROR);
                            }
                        }
                        else {
                            MessageBoxW(hDlg,
                                L"无法写入 exe 目录，请检查权限。",
                                L"错误", MB_ICONERROR);
                        }
                    }
                    else {
                        MessageBoxW(hDlg,
                            L"图标文件为空或过大（> 200KB）。",
                            L"提示", MB_ICONWARNING);
                    }
                }
            }
        }
        break;

        case IDC_BTN_TRAY_ICON_RESET:
        {
            std::wstring iconPath = GetExeDirectory() + L"custom.ico";
            DeleteFileW(iconPath.c_str());
            g_settings.customTrayIconFile.clear();
            PostMessage(g_hwnd, WM_APP + 202, 0, 0);
            MessageBoxW(hDlg, L"已恢复默认托盘图标。",
                L"提示", MB_OK | MB_ICONINFORMATION);
        }
        break;

        case IDC_BTN_EXPORT:
        {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = L"CameraMonitor_Config.cmc";
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"配置文件\0*.cmc\0所有文件\0*.*\0";
            ofn.lpstrDefExt = EXT_CONFIG;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) ExportConfig(path);
        }
        break;

        case IDC_BTN_IMPORT:
        {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = {};
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"配置文件\0*.cmc\0所有文件\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                if (ImportConfig(path))
                    MessageBoxW(hDlg, L"配置导入成功，请重启程序生效。", L"提示", MB_OK | MB_ICONINFORMATION);
                else
                    MessageBoxW(hDlg, L"配置导入失败。", L"错误", MB_ICONERROR);
            }
        }
        break;

        case IDC_BTN_ADD_SCRIPT:
        case IDM_NEW_SCRIPT:
        {
            std::vector<ScriptAction> newActions;
            if (ScriptEditor::Show(GetModuleHandle(NULL), hDlg,
                newActions, L"新脚本", true)) {
                int idx = g_scriptManager.AddScript(L"新脚本");
                ScriptInfo* info = g_scriptManager.GetScript(idx);
                if (info) info->actions = newActions;
                LoadScriptsToList(hDlg);
            }
        }
        break;

        case IDC_BTN_ADD_TIMERANGE:
        case IDM_NEW_RANGE:
        {
            if (TimeRangeEditor::Show(GetModuleHandle(NULL), hDlg))
                LoadRangesToList(hDlg);
        }
        break;

        case IDM_IMPORT_SCRIPT:
        {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = {};
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"脚本文件\0*.cms\0所有文件\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                if (!CheckFileHeader(hDlg, path, FileType::Script)) break;

                std::vector<ScriptAction> imported;
                if (ScriptSerializer::Import(path, imported)) {
                    int idx = g_scriptManager.AddScript(L"导入脚本");
                    ScriptInfo* info = g_scriptManager.GetScript(idx);
                    if (info) info->actions = imported;
                    LoadScriptsToList(hDlg);
                }
            }
        }
        break;

        case IDM_EDIT_SCRIPT:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPTS);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1) {
                ScriptInfo* info = g_scriptManager.GetScript(sel);
                if (info) {
                    if (ScriptEditor::Show(GetModuleHandle(NULL), hDlg,
                        info->actions, info->name, false)) {
                        LoadScriptsToList(hDlg);
                    }
                }
            }
        }
        break;

        case IDM_DEL_SCRIPT:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPTS);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1) {
                g_scriptManager.RemoveScript(sel);
                LoadScriptsToList(hDlg);
            }
        }
        break;

        case IDM_EXPORT_SCRIPT:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPTS);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel == -1) break;
            ScriptInfo* info = g_scriptManager.GetScript(sel);
            if (!info) break;

            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = L"script.cms";
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"脚本文件\0*.cms\0所有文件\0*.*\0";
            ofn.lpstrDefExt = EXT_SCRIPT;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) {
                if (ScriptSerializer::Export(path, info->actions)) {
                    MessageBoxW(hDlg, L"脚本导出成功。", L"提示", MB_OK | MB_ICONINFORMATION);
                }
            }
        }
        break;

        case IDM_TOGGLE_SCRIPT:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPTS);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1) {
                ScriptInfo* info = g_scriptManager.GetScript(sel);
                if (info) {
                    info->enabled = !info->enabled;
                    LoadScriptsToList(hDlg);
                }
            }
        }
        break;

        case IDM_IMPORT_RANGE:
        {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = {};
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"时间段文件\0*.cmt\0所有文件\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                if (!CheckFileHeader(hDlg, path, FileType::TimeRange)) break;

                FILE* f = nullptr;
                if (_wfopen_s(&f, path, L"r, ccs=UTF-8") == 0 && f) {
                    wchar_t line[1024];
                    bool firstLine = true;
                    std::vector<TimeRange> ranges;
                    while (fgetws(line, 1024, f)) {
                        if (firstLine) { firstLine = false; continue; }
                        std::wstring s = line;
                        if (!s.empty() && s.back() == L'\n') s.pop_back();
                        if (s.empty()) continue;

                        size_t p1 = s.find(L'|');
                        size_t p2 = s.find(L'|', p1 + 1);
                        if (p1 == std::wstring::npos || p2 == std::wstring::npos) continue;

                        TimeRange r;
                        swscanf_s(s.substr(0, p1).c_str(), L"%d:%d-%d:%d",
                            &r.startHour, &r.startMinute, &r.endHour, &r.endMinute);
                        std::wstring days = s.substr(p1 + 1, p2 - p1 - 1);
                        for (int d = 0; d < 7; d++)
                            r.days[d] = days.find(std::to_wstring(d)) != std::wstring::npos;
                        r.enabled = _wtoi(s.substr(p2 + 1).c_str()) != 0;
                        ranges.push_back(r);
                    }
                    fclose(f);
                    g_scheduleManager.SetRanges(ranges);
                    LoadRangesToList(hDlg);
                }
            }
        }
        break;

        case IDM_DEL_RANGE:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_TIMERANGES);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1) {
                auto ranges = g_scheduleManager.GetRanges();
                if (sel < (int)ranges.size()) {
                    ranges.erase(ranges.begin() + sel);
                    g_scheduleManager.SetRanges(ranges);
                    LoadRangesToList(hDlg);
                }
            }
        }
        break;

        case IDM_EXPORT_RANGE:
        {
            auto ranges = g_scheduleManager.GetRanges();
            if (ranges.empty()) break;

            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t path[MAX_PATH] = L"timerange.cmt";
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"时间段文件\0*.cmt\0所有文件\0*.*\0";
            ofn.lpstrDefExt = EXT_TIMERANGE;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) {
                FILE* f = nullptr;
                if (_wfopen_s(&f, path, L"w, ccs=UTF-8") == 0 && f) {
                    fwprintf(f, L"%s\n", HEADER_TIMERANGE);
                    for (const auto& r : ranges) {
                        std::wstring days;
                        for (int d = 0; d < 7; d++)
                            if (r.days[d]) days += std::to_wstring(d);
                        fwprintf(f, L"%02d:%02d-%02d:%02d|%s|%d\n",
                            r.startHour, r.startMinute, r.endHour, r.endMinute,
                            days.c_str(), r.enabled ? 1 : 0);
                    }
                    fclose(f);
                    MessageBoxW(hDlg, L"时间段导出成功。", L"提示", MB_OK | MB_ICONINFORMATION);
                }
            }
        }
        break;

        case IDM_TOGGLE_RANGE:
        {
            HWND hList = GetDlgItem(hDlg, IDC_LIST_TIMERANGES);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1) {
                auto ranges = g_scheduleManager.GetRanges();
                if (sel < (int)ranges.size()) {
                    ranges[sel].enabled = !ranges[sel].enabled;
                    g_scheduleManager.SetRanges(ranges);
                    LoadRangesToList(hDlg);
                }
            }
        }
        break;

        case IDC_BTN_SAVE:
            OnSave(hDlg);
            break;

        case IDC_BTN_CANCEL:
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void SettingsDialog::OnSave(HWND hDlg)
{
    g_settings.hideTrayIcon = (IsDlgButtonChecked(hDlg, IDC_CHK_HIDE_TRAY) == BST_CHECKED);
    g_settings.logToFile = (IsDlgButtonChecked(hDlg, IDC_CHK_LOG) == BST_CHECKED);
    wchar_t buf[MAX_PATH];
    GetDlgItemTextW(hDlg, IDC_EDIT_LOG_PATH, buf, MAX_PATH);
    g_settings.logPath = buf;
    GetDlgItemTextW(hDlg, IDC_EDIT_HOTKEY, buf, MAX_PATH);
    g_settings.hotkey = buf;
    g_settings.storage = IsDlgButtonChecked(hDlg, IDC_RADIO_REG) == BST_CHECKED ? StorageType::Registry : StorageType::ExeDirectory;
    g_storageType = g_settings.storage;

    ConfigStore::SaveBool(L"HideTrayIcon", g_settings.hideTrayIcon);
    ConfigStore::SaveBool(L"LogToFile", g_settings.logToFile);
    ConfigStore::SaveString(L"LogPath", g_settings.logPath);
    ConfigStore::SaveString(L"Hotkey", g_settings.hotkey);
    ConfigStore::SaveInt(L"Volume", g_settings.volume);
    ConfigStore::SaveInt(L"Storage", (int)g_settings.storage);
    ConfigStore::SaveString(L"Password", g_settings.password);
    ConfigStore::SaveString(L"TrayIconFile", g_settings.customTrayIconFile);

    g_scriptManager.SaveAll();

    auto ranges = g_scheduleManager.GetRanges();
    ConfigStore::SaveInt(L"TimeRangeCount", (int)ranges.size());
    for (size_t i = 0; i < ranges.size(); i++) {
        wchar_t prefix[32];
        swprintf_s(prefix, L"TimeRange%zu_", i);
        ConfigStore::SaveInt(std::wstring(prefix) + L"StartHour", ranges[i].startHour);
        ConfigStore::SaveInt(std::wstring(prefix) + L"StartMinute", ranges[i].startMinute);
        ConfigStore::SaveInt(std::wstring(prefix) + L"EndHour", ranges[i].endHour);
        ConfigStore::SaveInt(std::wstring(prefix) + L"EndMinute", ranges[i].endMinute);
        ConfigStore::SaveInt(std::wstring(prefix) + L"Enabled", ranges[i].enabled ? 1 : 0);
        for (int d = 0; d < 7; d++) {
            wchar_t dayName[32];
            swprintf_s(dayName, L"%sDay%d", prefix, d);
            ConfigStore::SaveInt(dayName, ranges[i].days[d] ? 1 : 0);
        }
    }

    PostMessage(g_hwnd, WM_APP + 200, 0, 0);

    MessageBoxW(hDlg, L"设置已保存", L"提示", MB_OK | MB_ICONINFORMATION);
    EndDialog(hDlg, IDOK);
}

INT_PTR SettingsDialog::Show(HINSTANCE hInst, HWND parent)
{
    if (!CheckPassword(parent)) return IDCANCEL;
    return DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG), NULL, DlgProc, 0);
}