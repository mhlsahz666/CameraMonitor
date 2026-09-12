#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <shellapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <mutex>

#include "ScriptEngine.h"
#include "ScheduleManager.h"
#include "AutoStart.h"
#include "SettingsDialog.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfsensorgroup.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

using namespace Microsoft::WRL;

#define WM_TRAYICON       (WM_USER + 1)
#define WM_CAMERA_CHANGE  (WM_USER + 2)
#define WM_SCHEDULE_EXIT  (WM_USER + 10)

#define ID_TRAY_EXIT      1001
#define ID_TRAY_SHOW      1002
#define ID_TRAY_SETTINGS  1003
#define ID_TRAY_AUTOSTART 1005

NOTIFYICONDATAW g_nid = {};
HWND            g_hwnd = nullptr;
HINSTANCE       g_hinst = nullptr;
ComPtr<IMFSensorActivityMonitor> g_monitor;
bool            g_cameraInUse = false;
std::mutex      g_stateMutex;

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
        for (ULONG i = 0; i < count; i++) {
            ComPtr<IMFSensorActivityReport> activity;
            if (FAILED(report->GetActivityReport(i, &activity))) continue;
            ULONG procCount = 0;
            if (FAILED(activity->GetProcessCount(&procCount))) continue;
            for (ULONG j = 0; j < procCount; j++) {
                ComPtr<IMFSensorProcessActivity> procAct;
                if (FAILED(activity->GetProcessActivity(j, &procAct))) continue;
                BOOL streaming = FALSE;
                if (SUCCEEDED(procAct->GetStreamingState(&streaming)) && streaming) {
                    anyStreaming = true;
                    break;
                }
            }
            if (anyStreaming) break;
        }

        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (anyStreaming != g_cameraInUse) {
                g_cameraInUse = anyStreaming;
                changed = true;
            }
        }
        if (changed) {
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
    wcscpy_s(g_nid.szTip, inUse ? L"摄像头监控 - 正在被占用" : L"摄像头监控 - 空闲");
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ======================== 加载配置 ========================
void LoadConfigFromRegistry()
{
    // 时间段
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
            r.enabled = ConfigStore::LoadInt(std::wstring(prefix) + L"Enabled", 1) != 0;
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

    // 脚本
    auto loadScript = [](const std::wstring& prefix) {
        std::vector<ScriptAction> actions;
        int count = ConfigStore::LoadInt(prefix + L"Count", 0);
        for (int i = 0; i < count; i++) {
            wchar_t idx[32];
            swprintf_s(idx, L"%d_", i);
            std::wstring p = prefix + idx;
            ScriptAction a;
            a.type = (ScriptActionType)ConfigStore::LoadInt(p + L"Type", 0);
            a.param1 = ConfigStore::LoadString(p + L"Param1", L"");
            a.param2 = ConfigStore::LoadString(p + L"Param2", L"");
            a.repeatCount = ConfigStore::LoadInt(p + L"RepeatCount", 1);
            a.delay = ConfigStore::LoadInt(p + L"Delay", 0);
            a.soundWait = ConfigStore::LoadInt(p + L"SoundWait", 0);
            actions.push_back(a);
        }
        return actions;
        };

    auto startScript = loadScript(L"ScriptStart_");
    auto stopScript = loadScript(L"ScriptStop_");
    if (!startScript.empty())
        g_scriptEngine.SetScript(ScriptEvent::CameraStart, startScript);
    if (!stopScript.empty())
        g_scriptEngine.SetScript(ScriptEvent::CameraStop, stopScript);

    // 默认脚本（首次运行）
    if (g_scriptEngine.GetScript(ScriptEvent::CameraStart).empty()) {
        ScriptAction a1;
        a1.type = ScriptActionType::CustomNotify;
        a1.param1 = L"摄像头监控";
        a1.param2 = L"摄像头已被占用";
        g_scriptEngine.SetScript(ScriptEvent::CameraStart, { a1 });
    }
    if (g_scriptEngine.GetScript(ScriptEvent::CameraStop).empty()) {
        ScriptAction a2;
        a2.type = ScriptActionType::CustomNotify;
        a2.param1 = L"摄像头监控";
        a2.param2 = L"摄像头已空闲";
        g_scriptEngine.SetScript(ScriptEvent::CameraStop, { a2 });
    }
}

// ======================== 窗口过程 ========================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CameraMonitorTrayWnd";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"CameraMonitorTrayWnd", L"",
        0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hwnd) return 1;

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(g_nid.szTip, L"摄像头监控 - 初始化中...");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // 从注册表加载配置
    LoadConfigFromRegistry();
    // 启动时立即检查是否在退出时间段内
    if (!g_scheduleManager.IsInAllowedTime()) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 0;
    }
    g_scheduleManager.Start(g_hwnd);

    if (!InitCameraMonitor(g_hwnd)) {
        MessageBoxW(NULL,
            L"无法启动摄像头监控。\n\n"
            L"请确认：\n"
            L"1. 系统为 Windows 10 1703 或更高版本\n"
            L"2. 摄像头驱动正常",
            L"错误", MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}