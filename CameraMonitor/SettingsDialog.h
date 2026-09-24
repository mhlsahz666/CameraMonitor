#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include "ScriptEngine.h"
#include "ScheduleManager.h"
#include "AutoStart.h"
#include "ScriptManager.h"
#include "FileHeader.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

struct AppSettings {
    bool hideTrayIcon = false;
    bool logToFile = false;
    std::wstring logPath = L"D:\\CameraMonitor.log";
    std::wstring hotkey = L"Control+Alt+C";
    int volume = 50;
    StorageType storage = StorageType::Registry;
    std::wstring password;
    std::wstring customTrayIconFile;   // 自定义托盘图标文件名（如 "custom.ico"）
};

class SettingsDialog {
public:
    static INT_PTR Show(HINSTANCE hInst, HWND parent);
    static AppSettings g_settings;
    static ScriptManager g_scriptManager;

    static HWND s_hDlg;
    static HWND s_hTab;
    static std::vector<HWND> s_pageCtrls[3];

private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static void OnInit(HWND hDlg);
    static void OnTabChanged(HWND hDlg, int sel);
    static void OnSave(HWND hDlg);
    static void ShowPage(HWND hDlg, int page);

    static void OnInitSettingsPage(HWND hDlg);
    static void OnInitScriptsPage(HWND hDlg);
    static void OnInitTimerPage(HWND hDlg);
    static void LoadScriptsToList(HWND hDlg);
    static void LoadRangesToList(HWND hDlg);

    static INT_PTR CALLBACK PasswordDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK PasswordInputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static bool CheckPassword(HWND parent);
    static bool ExportConfig(const std::wstring& path);
    static bool ImportConfig(const std::wstring& path);
};