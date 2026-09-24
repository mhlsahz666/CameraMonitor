#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <shellapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <mutex>

#include "ScriptEngine.h"
#include "ScheduleManager.h"
#include "AutoStart.h"
#include "SettingsDialog.h"
#include "Logger.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfsensorgroup.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::WRL;

#define WM_TRAYICON       (WM_USER + 1)
#define WM_CAMERA_CHANGE  (WM_USER + 2)
#define WM_SCHEDULE_EXIT  (WM_USER + 10)

#define ID_TRAY_EXIT      1001
#define ID_TRAY_SHOW      1002
#define ID_TRAY_SETTINGS  1003
#define ID_TRAY_AUTOSTART 1005

// ======================== 全局变量 ========================
NOTIFYICONDATAW g_nid = {};
HWND            g_hwnd = nullptr;
HINSTANCE       g_hinst = nullptr;
ComPtr<IMFSensorActivityMonitor> g_monitor;
bool            g_cameraInUse = false;
std::wstring    g_currentOccupiedCamera;
std::wstring    g_currentOccupiedProcess;
std::mutex      g_stateMutex;
UINT            g_uTaskbarCreatedMsg = 0;

ScriptEngine    g_scriptEngine;
ScheduleManager g_scheduleManager;

// ======================== 摄像头回调 ========================
class CameraActivityCallback : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    IMFSensorActivitiesReportCallback, FtmBase>
{
public:
    CameraActivityCallback(HWND hwnd) : m_hwnd(hwnd) {}

    IFACEMETHODIMP OnActivitiesReport(_In_ IMFSensorActivitiesReport* report) override
    {
        if (!report) return S_OK;
        ULONG count = 0;
        if (FAILED(report->GetCount(&count))) return S_OK;

        bool anyStreaming = false;
        std::wstring deviceName;
        std::wstring processName;
        DWORD pid = 0;

        for (ULONG i = 0; i < count; i++) {
            ComPtr<IMFSensorActivityReport> activity;
            if (FAILED(report->GetActivityReport(i, &activity))) continue;

            WCHAR friendlyName[512] = {};
            ULONG nameLen = 512;
            if (SUCCEEDED(activity->GetFriendlyName(friendlyName, nameLen, &nameLen))) {
                deviceName = friendlyName;
            }

            ULONG procCount = 0;
            if (FAILED(activity->GetProcessCount(&procCount))) continue;
            for (ULONG j = 0; j < procCount; j++) {
                ComPtr<IMFSensorProcessActivity> procAct;
                if (FAILED(activity->GetProcessActivity(j, &procAct))) continue;
                BOOL streaming = FALSE;
                if (SUCCEEDED(procAct->GetStreamingState(&streaming)) && streaming) {
                    anyStreaming = true;
                    ULONG pidU = 0;
                    if (SUCCEEDED(procAct->GetProcessId(&pidU))) {
                        pid = (DWORD)pidU;
                        HANDLE hProc = OpenProcess(
                            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                        if (hProc) {
                            wchar_t path[MAX_PATH] = {};
                            DWORD size = MAX_PATH;
                            if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
                                std::wstring p = path;
                                size_t pos = p.find_last_of(L"\\/");
                                processName = (pos != std::wstring::npos)
                                    ? p.substr(pos + 1) : p;
                            }
                            CloseHandle(hProc);
                        }
                    }
                    break;
                }
            }
            if (anyStreaming) break;
        }

        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_currentOccupiedCamera = deviceName;
            g_currentOccupiedProcess = processName;
            if (anyStreaming != g_cameraInUse) {
                g_cameraInUse = anyStreaming;
                changed = true;
            }
        }
        if (changed) {
            if (anyStreaming) {
                Logger::CameraOccupied(deviceName, processName, pid);
            }
            else {
                Logger::CameraReleased(deviceName);
            }
            PostMessage(m_hwnd, WM_CAMERA_CHANGE, anyStreaming ? 1 : 0, 0);
        }
        return S_OK;
    }
private:
    HWND m_hwnd;
};

// ======================== 托盘提示 ========================
void UpdateTrayTip(bool inUse)
{
    if (SettingsDialog::g_settings.hideTrayIcon) return;
    wcscpy_s(g_nid.szTip, inUse ? L"摄像头监控 - 正在被占用" : L"摄像头监控 - 空闲");
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ======================== 热键解析 ========================
bool ParseHotkey(const std::wstring& hotkey, UINT& modifiers, UINT& vk)
{
    modifiers = 0;
    vk = 0;
    std::wstring str = hotkey;

    if (str.find(L"Control+") != std::wstring::npos) {
        modifiers |= MOD_CONTROL;
        size_t pos = str.find(L'+');
        if (pos != std::wstring::npos) str = str.substr(pos + 1);
    }
    if (str.find(L"Alt+") != std::wstring::npos) {
        modifiers |= MOD_ALT;
        size_t pos = str.find(L'+');
        if (pos != std::wstring::npos) str = str.substr(pos + 1);
    }
    if (str.find(L"Shift+") != std::wstring::npos) {
        modifiers |= MOD_SHIFT;
        size_t pos = str.find(L'+');
        if (pos != std::wstring::npos) str = str.substr(pos + 1);
    }

    if (str.length() == 1 &&
        ((str[0] >= 'A' && str[0] <= 'Z') || (str[0] >= '0' && str[0] <= '9'))) {
        vk = str[0];
    }
    else if (str.substr(0, 3) == L"VK_") {
        vk = wcstoul(str.substr(3).c_str(), nullptr, 16);
    }
    return vk != 0;
}

// ======================== 加载配置 ========================
void LoadConfig()
{
    // 1. 判断存储方式
    if (ConfigStore::FileConfigExists()) {
        g_storageType = StorageType::ExeDirectory;
    }
    else {
        g_storageType = StorageType::Registry;
    }
    SettingsDialog::g_settings.storage = g_storageType;

    // 2. 读取设置
    SettingsDialog::g_settings.hideTrayIcon = ConfigStore::LoadBool(L"HideTrayIcon", false);
    SettingsDialog::g_settings.logToFile = ConfigStore::LoadBool(L"LogToFile", false);
    SettingsDialog::g_settings.logPath = ConfigStore::LoadString(L"LogPath", L"D:\\CameraMonitor.log");
    SettingsDialog::g_settings.hotkey = ConfigStore::LoadString(L"Hotkey", L"Control+Alt+C");
    SettingsDialog::g_settings.volume = ConfigStore::LoadInt(L"Volume", 50);
    SettingsDialog::g_settings.password = ConfigStore::LoadString(L"Password", L"");
    SettingsDialog::g_settings.customTrayIconFile = ConfigStore::LoadString(L"TrayIconFile", L"");

    // 3. 加载时间段
    int rangeCount = ConfigStore::LoadInt(L"TimeRangeCount", -1);
    if (rangeCount > 0) {
        std::vector<TimeRange> ranges;
        for (int i = 0; i < rangeCount; i++) {
            wchar_t prefix[32];
            swprintf_s(prefix, L"TimeRange%d_", i);
            TimeRange r;
            r.startHour = ConfigStore::LoadInt(std::wstring(prefix) + L"StartHour", 8);
            r.startMinute = ConfigStore::LoadInt(std::wstring(prefix) + L"StartMinute", 0);
            r.endHour = ConfigStore::LoadInt(std::wstring(prefix) + L"EndHour", 22);
            r.endMinute = ConfigStore::LoadInt(std::wstring(prefix) + L"EndMinute", 0);
            r.enabled = ConfigStore::LoadInt(std::wstring(prefix) + L"Enabled", 0) != 0;
            for (int d = 0; d < 7; d++) {
                wchar_t dayName[32];
                swprintf_s(dayName, L"%sDay%d", prefix, d);
                r.days[d] = ConfigStore::LoadInt(dayName, 1) != 0;
            }
            ranges.push_back(r);
        }
        g_scheduleManager.SetRanges(ranges);
    }
    else {
        std::vector<TimeRange> ranges;
        TimeRange r;
        ranges.push_back(r);
        g_scheduleManager.SetRanges(ranges);
    }
}

// ======================== 窗口过程 ========================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 处理 explorer 重启
    if (msg == g_uTaskbarCreatedMsg && g_uTaskbarCreatedMsg != 0) {
        if (!SettingsDialog::g_settings.hideTrayIcon) {
            Shell_NotifyIconW(NIM_ADD, &g_nid);
        }
        return 0;
    }

    switch (msg)
    {
    case WM_CAMERA_CHANGE:
    {
        bool inUse = (wParam == 1);
        UpdateTrayTip(inUse);
        if (inUse)
            g_scriptEngine.TriggerEvent(ScriptEvent::CameraStart);
        else
            g_scriptEngine.TriggerEvent(ScriptEvent::CameraStop);
    }
    return 0;

    case WM_SCHEDULE_EXIT:
        DestroyWindow(hwnd);
        return 0;

    case WM_HOTKEY:
        if (wParam == 1) {
            SettingsDialog::Show(g_hinst, NULL);
        }
        return 0;

    case WM_APP + 200:
        if (SettingsDialog::g_settings.hideTrayIcon) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
        }
        else {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            Shell_NotifyIconW(NIM_ADD, &g_nid);
        }
        return 0;

    case WM_APP + 201:
    {
        HICON hIcon = (HICON)wParam;
        if (hIcon) {
            g_nid.hIcon = hIcon;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        }
    }
    return 0;

    case WM_APP + 202:
    {
        HICON hIcon = LoadIconW(g_hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
        if (!hIcon) hIcon = LoadIconW(NULL, IDI_APPLICATION);
        g_nid.hIcon = hIcon;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
    return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示主窗口");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"设置");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING | (AutoStart::IsEnabled(L"CameraMonitor") ? MF_CHECKED : 0),
                ID_TRAY_AUTOSTART, L"开机自启");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出程序");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        case ID_TRAY_SHOW:
        case ID_TRAY_SETTINGS:
            SettingsDialog::Show(g_hinst, hwnd);
            break;
        case ID_TRAY_AUTOSTART:
        {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            if (AutoStart::IsEnabled(L"CameraMonitor"))
                AutoStart::Disable(L"CameraMonitor");
            else
                AutoStart::Enable(L"CameraMonitor", path);
        }
        break;
        }
        return 0;

    case WM_DESTROY:
        Logger::ProgramStop();
        UnregisterHotKey(hwnd, 1);
        g_scheduleManager.Stop();
        if (g_monitor) { g_monitor->Stop(); g_monitor.Reset(); }
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        MFShutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ======================== 初始化监控 ========================
bool InitCameraMonitor(HWND hwnd)
{
    if (FAILED(MFStartup(MF_VERSION))) return false;
    ComPtr<CameraActivityCallback> callback = Make<CameraActivityCallback>(hwnd);
    if (!callback) return false;
    if (FAILED(MFCreateSensorActivityMonitor(callback.Get(), &g_monitor))) return false;
    return SUCCEEDED(g_monitor->Start());
}

// ======================== 入口 ========================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    g_hinst = hInstance;

    // 1. 初始化 Common Controls
    INITCOMMONCONTROLSEX icc = { sizeof(icc) };
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    // 2. 注册窗口类
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CameraMonitorTrayWnd";
    RegisterClassExW(&wc);

    // 3. 创建隐藏主窗口
    g_hwnd = CreateWindowExW(0, L"CameraMonitorTrayWnd", L"",
        0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hwnd) return 1;

    // 4. 加载配置
    LoadConfig();

    // 5. 注册 TaskbarCreated 消息
    g_uTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // 6. 初始化托盘图标
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;

    // 从 exe 同目录加载自定义托盘图标
    HICON hTrayIcon = nullptr;
    if (!SettingsDialog::g_settings.customTrayIconFile.empty()) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring dir = exePath;
        size_t pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) dir = dir.substr(0, pos + 1);

        std::wstring iconPath = dir + SettingsDialog::g_settings.customTrayIconFile;

        hTrayIcon = (HICON)LoadImageW(NULL, iconPath.c_str(), IMAGE_ICON,
            0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }
    if (!hTrayIcon) {
        hTrayIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    }
    if (!hTrayIcon) {
        hTrayIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    g_nid.hIcon = hTrayIcon;

    wcscpy_s(g_nid.szTip, L"摄像头监控 - 初始化中...");

    // 7. 添加到托盘
    if (!SettingsDialog::g_settings.hideTrayIcon) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
    }

    // 8. 注册热键
    if (!SettingsDialog::g_settings.hotkey.empty()) {
        UINT modifiers = 0, vk = 0;
        if (ParseHotkey(SettingsDialog::g_settings.hotkey, modifiers, vk)) {
            RegisterHotKey(g_hwnd, 1, modifiers | MOD_NOREPEAT, vk);
        }
    }

    // 9. 启动定时退出管理
    g_scheduleManager.Start(g_hwnd);

    if (!g_scheduleManager.IsInAllowedTime()) {
        if (!SettingsDialog::g_settings.hideTrayIcon) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
        }
        return 0;
    }

    // 10. 初始化摄像头监控
    if (!InitCameraMonitor(g_hwnd)) {
        MessageBoxW(NULL,
            L"无法启动摄像头监控。\n\n"
            L"请确认：\n"
            L"1. 系统为 Windows 10 1703 或更高版本\n"
            L"2. 摄像头驱动正常",
            L"错误", MB_ICONERROR);
        if (!SettingsDialog::g_settings.hideTrayIcon) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
        }
        return 1;
    }

    // 11. 消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}