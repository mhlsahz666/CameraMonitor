#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include "ScriptEngine.h"
#include "ScheduleManager.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

class SettingsDialog {
public:
    static INT_PTR Show(HINSTANCE hInst, HWND parent);
    static HWND s_hDlg;
    static HWND s_hTab;
    static std::vector<HWND> s_pageCtrls[3];

private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static void OnInit(HWND hDlg);
    static void OnTabChanged(HWND hDlg, int sel);
    static void OnSave(HWND hDlg);
    static void OnRefreshCameras(HWND hDlg);
    static void OnAddScript(HWND hDlg, bool isStart);
    static void OnDelScript(HWND hDlg, bool isStart);
    static void OnEditScript(HWND hDlg, bool isStart);
    static void OnMoveScript(HWND hDlg, bool isStart, bool up);
    static void OnAddRange(HWND hDlg);
    static void OnDelRange(HWND hDlg);
    static void LoadRangesToList(HWND hDlg);
    static void LoadScriptsToList(HWND hDlg);
    static void ShowPage(HWND hDlg, int page);

    static bool EditScriptAction(HWND parent, ScriptAction& action, bool isNew);
    static INT_PTR CALLBACK ScriptEditorProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static ScriptAction* s_editingAction;

    // 键盘录制
    static bool s_recording;
    static HWND s_recordBtn;
    static UINT_PTR s_recordTimer;
    static std::wstring s_recordedKey;
    static HWND s_recordDlg;
    static void StartKeyRecord(HWND hDlg);
    static void StopKeyRecord();
    static std::wstring VkToName(DWORD vk);
};