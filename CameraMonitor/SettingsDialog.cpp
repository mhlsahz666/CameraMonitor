#include "SettingsDialog.h"
#include "AutoStart.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <objbase.h>
#include <comdef.h>
#include <commdlg.h>
#include <string>
#include <sstream>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")

extern ScriptEngine    g_scriptEngine;
extern ScheduleManager g_scheduleManager;
extern HWND            g_hwnd;

HWND SettingsDialog::s_hDlg = nullptr;
HWND SettingsDialog::s_hTab = nullptr;
std::vector<HWND> SettingsDialog::s_pageCtrls[3];
ScriptAction* SettingsDialog::s_editingAction = nullptr;

// 键盘录制静态成员
bool SettingsDialog::s_recording = false;
HWND SettingsDialog::s_recordBtn = nullptr;
UINT_PTR SettingsDialog::s_recordTimer = 0;
std::wstring SettingsDialog::s_recordedKey;
HWND SettingsDialog::s_recordDlg = nullptr;

#define RECORD_TIMER_ID 9999

// ======================== 辅助：创建控件 ========================
static HWND MakeCtrl(HWND parent, const wchar_t* cls, const wchar_t* text,
    DWORD style, int x, int y, int w, int h, int id, int page)
{
    HWND hCtrl = CreateWindowExW(0, cls, text, WS_CHILD | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (page >= 0 && page < 3)
        SettingsDialog::s_pageCtrls[page].push_back(hCtrl);
    return hCtrl;
}

// ======================== 动作类型名称 ========================
static const wchar_t* ActionTypeName(ScriptActionType t)
{
    switch (t) {
    case ScriptActionType::RunCmd:          return L"执行 CMD 命令";
    case ScriptActionType::PlaySoundAction: return L"播放声音";
    case ScriptActionType::SimulateKey:     return L"模拟按键";
    case ScriptActionType::CreateWindow:    return L"弹出窗口";
    case ScriptActionType::CustomNotify:    return L"自定义通知";
    case ScriptActionType::Wait:            return L"等待";
    case ScriptActionType::Repeat:          return L"重复执行";
    case ScriptActionType::IfCameraInUse:   return L"如果摄像头被占用";
    case ScriptActionType::ExitProgram:     return L"退出程序";
    }
    return L"未知";
}

// ======================== 动作提示 ========================
static const wchar_t* ActionHint(ScriptActionType t)
{
    switch (t) {
    case ScriptActionType::RunCmd:
        return L"参数 1：要执行的命令，例如 notepad.exe";
    case ScriptActionType::PlaySoundAction:
        return L"参数 1：声音文件完整路径，例如 C:\\Windows\\Media\\notify.wav\n"
            L"播放方式：不等待=后台播放，等待=播放完毕后继续执行下一个动作。";
    case ScriptActionType::SimulateKey:
        return L"参数 1：按键组合。可在“单键”下拉框选择，或点击“录制”按下键盘按键。";
    case ScriptActionType::CreateWindow:
        return L"参数 1：窗口标题\n参数 2：窗口内容";
    case ScriptActionType::CustomNotify:
        return L"参数 1：通知标题\n参数 2：通知内容";
    case ScriptActionType::Wait:
        return L"参数 1：等待毫秒数";
    case ScriptActionType::Repeat:
        return L"参数 1：重复次数";
    case ScriptActionType::IfCameraInUse:
        return L"条件动作";
    case ScriptActionType::ExitProgram:
        return L"无需参数。执行后退出程序。";
    }
    return L"";
}

// ======================== 初始化对话框 ========================
void SettingsDialog::OnInit(HWND hDlg)
{
    s_hDlg = hDlg;
    s_hTab = GetDlgItem(hDlg, IDC_TAB);

    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    const wchar_t* titles[] = { L"摄像头", L"脚本", L"定时与自启" };
    for (int i = 0; i < 3; i++) {
        tie.pszText = (LPWSTR)titles[i];
        TabCtrl_InsertItem(s_hTab, i, &tie);
    }

    const int PX = 15;
    const int PY = 35;

    // 页面 0：摄像头
    MakeCtrl(hDlg, L"STATIC", L"已选摄像头（可多选）：",
        SS_LEFT, PX, PY, 280, 20, -1, 0);
    MakeCtrl(hDlg, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_EXTENDEDSEL | LBS_NOTIFY,
        PX, PY + 25, 200, 150, IDC_LIST_CAM, 0);
    MakeCtrl(hDlg, L"BUTTON", L"刷新列表",
        BS_PUSHBUTTON, PX + 210, PY + 25, 70, 26, IDC_BTN_REFRESH, 0);

    // 页面 1：脚本
    MakeCtrl(hDlg, L"STATIC", L"摄像头开始占用时执行：",
        SS_LEFT, PX, PY, 280, 20, -1, 1);
    MakeCtrl(hDlg, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        PX, PY + 25, 200, 70, IDC_LIST_START, 1);
    MakeCtrl(hDlg, L"BUTTON", L"添加",
        BS_PUSHBUTTON, PX + 208, PY + 25, 60, 22, IDC_BTN_ADD_START, 1);
    MakeCtrl(hDlg, L"BUTTON", L"编辑",
        BS_PUSHBUTTON, PX + 208, PY + 49, 60, 22, IDC_BTN_EDIT_START, 1);
    MakeCtrl(hDlg, L"BUTTON", L"删除",
        BS_PUSHBUTTON, PX + 208, PY + 73, 60, 22, IDC_BTN_DEL_START, 1);
    MakeCtrl(hDlg, L"BUTTON", L"↑",
        BS_PUSHBUTTON, PX + 270, PY + 25, 28, 22, IDC_BTN_UP_START, 1);
    MakeCtrl(hDlg, L"BUTTON", L"↓",
        BS_PUSHBUTTON, PX + 270, PY + 49, 28, 22, IDC_BTN_DOWN_START, 1);

    MakeCtrl(hDlg, L"STATIC", L"摄像头停止占用时执行：",
        SS_LEFT, PX, PY + 105, 280, 20, -1, 1);
    MakeCtrl(hDlg, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        PX, PY + 130, 200, 70, IDC_LIST_STOP, 1);
    MakeCtrl(hDlg, L"BUTTON", L"添加",
        BS_PUSHBUTTON, PX + 208, PY + 130, 60, 22, IDC_BTN_ADD_STOP, 1);
    MakeCtrl(hDlg, L"BUTTON", L"编辑",
        BS_PUSHBUTTON, PX + 208, PY + 154, 60, 22, IDC_BTN_EDIT_STOP, 1);
    MakeCtrl(hDlg, L"BUTTON", L"删除",
        BS_PUSHBUTTON, PX + 208, PY + 178, 60, 22, IDC_BTN_DEL_STOP, 1);
    MakeCtrl(hDlg, L"BUTTON", L"↑",
        BS_PUSHBUTTON, PX + 270, PY + 130, 28, 22, IDC_BTN_UP_STOP, 1);
    MakeCtrl(hDlg, L"BUTTON", L"↓",
        BS_PUSHBUTTON, PX + 270, PY + 154, 28, 22, IDC_BTN_DOWN_STOP, 1);

    // 页面 2：定时与自启
    MakeCtrl(hDlg, L"STATIC", L"退出程序的时间段（在该时间段内程序不运行）：",
        SS_LEFT, PX, PY, 280, 20, -1, 2);
    MakeCtrl(hDlg, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        PX, PY + 25, 220, 70, IDC_LIST_TIMERANGE, 2);
    MakeCtrl(hDlg, L"BUTTON", L"添加",
        BS_PUSHBUTTON, PX + 228, PY + 25, 60, 24, IDC_BTN_ADD_RANGE, 2);
    MakeCtrl(hDlg, L"BUTTON", L"删除",
        BS_PUSHBUTTON, PX + 228, PY + 53, 60, 24, IDC_BTN_DEL_RANGE, 2);

    MakeCtrl(hDlg, L"STATIC", L"开始：", SS_LEFT, PX, PY + 105, 40, 20, -1, 2);
    MakeCtrl(hDlg, L"EDIT", L"13",
        WS_BORDER | ES_NUMBER, PX + 45, PY + 103, 35, 22, IDC_EDIT_STARTHH, 2);
    MakeCtrl(hDlg, L"STATIC", L":", SS_LEFT, PX + 82, PY + 105, 10, 20, -1, 2);
    MakeCtrl(hDlg, L"EDIT", L"00",
        WS_BORDER | ES_NUMBER, PX + 92, PY + 103, 35, 22, IDC_EDIT_STARTMM, 2);
    MakeCtrl(hDlg, L"STATIC", L"结束：", SS_LEFT, PX + 140, PY + 105, 40, 20, -1, 2);
    MakeCtrl(hDlg, L"EDIT", L"14",
        WS_BORDER | ES_NUMBER, PX + 185, PY + 103, 35, 22, IDC_EDIT_ENDHH, 2);
    MakeCtrl(hDlg, L"STATIC", L":", SS_LEFT, PX + 222, PY + 105, 10, 20, -1, 2);
    MakeCtrl(hDlg, L"EDIT", L"00",
        WS_BORDER | ES_NUMBER, PX + 232, PY + 103, 35, 22, IDC_EDIT_ENDMM, 2);

    // 星期选择
    MakeCtrl(hDlg, L"STATIC", L"星期：", SS_LEFT, PX, PY + 135, 40, 20, -1, 2);
    {
        const wchar_t* dayNames[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
        int dayIds[] = { IDC_CHK_DAY_SUN, IDC_CHK_DAY_MON, IDC_CHK_DAY_TUE,
                         IDC_CHK_DAY_WED, IDC_CHK_DAY_THU, IDC_CHK_DAY_FRI,
                         IDC_CHK_DAY_SAT };
        for (int i = 0; i < 7; i++) {
            MakeCtrl(hDlg, L"BUTTON", dayNames[i],
                BS_AUTOCHECKBOX, PX + 45 + i * 30, PY + 133, 28, 22, dayIds[i], 2);
            CheckDlgButton(hDlg, dayIds[i], BST_CHECKED);
        }
    }

    MakeCtrl(hDlg, L"BUTTON", L"开机自动启动",
        BS_AUTOCHECKBOX, PX, PY + 165, 200, 22, IDC_CHK_AUTOSTART, 2);

    // 设置字体
    HFONT hFont = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");
    for (int p = 0; p < 3; p++)
        for (HWND h : s_pageCtrls[p])
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 加载当前设置
    CheckDlgButton(hDlg, IDC_CHK_AUTOSTART,
        AutoStart::IsEnabled(L"CameraMonitor") ? BST_CHECKED : BST_UNCHECKED);

    LoadRangesToList(hDlg);
    LoadScriptsToList(hDlg);

    ShowPage(hDlg, 0);
}

// ======================== 页面切换 ========================
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

// ======================== 脚本列表 ========================
void SettingsDialog::LoadScriptsToList(HWND hDlg)
{
    HWND hStart = GetDlgItem(hDlg, IDC_LIST_START);
    HWND hStop = GetDlgItem(hDlg, IDC_LIST_STOP);
    SendMessageW(hStart, LB_RESETCONTENT, 0, 0);
    SendMessageW(hStop, LB_RESETCONTENT, 0, 0);

    auto startActions = g_scriptEngine.GetScript(ScriptEvent::CameraStart);
    for (size_t i = 0; i < startActions.size(); i++) {
        std::wstring text = std::to_wstring(i + 1) + L". " +
            std::wstring(ActionTypeName(startActions[i].type));
        if (!startActions[i].param1.empty())
            text += L" [" + startActions[i].param1 + L"]";
        SendMessageW(hStart, LB_ADDSTRING, 0, (LPARAM)text.c_str());
    }

    auto stopActions = g_scriptEngine.GetScript(ScriptEvent::CameraStop);
    for (size_t i = 0; i < stopActions.size(); i++) {
        std::wstring text = std::to_wstring(i + 1) + L". " +
            std::wstring(ActionTypeName(stopActions[i].type));
        if (!stopActions[i].param1.empty())
            text += L" [" + stopActions[i].param1 + L"]";
        SendMessageW(hStop, LB_ADDSTRING, 0, (LPARAM)text.c_str());
    }
}

// ======================== 时间段列表 ========================
void SettingsDialog::LoadRangesToList(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_TIMERANGE);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    auto ranges = g_scheduleManager.GetRanges();
    for (size_t i = 0; i < ranges.size(); i++) {
        const TimeRange& r = ranges[i];
        std::wstring daysStr;
        const wchar_t* dayShort[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
        for (int d = 0; d < 7; d++) {
            if (r.days[d]) daysStr += dayShort[d];
        }
        if (daysStr.empty()) daysStr = L"无";

        wchar_t buf[128];
        swprintf_s(buf, L"退出：%02d:%02d-%02d:%02d [%s]",
            r.startHour, r.startMinute, r.endHour, r.endMinute,
            daysStr.c_str());
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

// ======================== 保存 ========================
void SettingsDialog::OnSave(HWND hDlg)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    bool autoStart = (IsDlgButtonChecked(hDlg, IDC_CHK_AUTOSTART) == BST_CHECKED);
    ConfigStore::SaveBool(L"AutoStart", autoStart);
    if (autoStart)
        AutoStart::Enable(L"CameraMonitor", exePath);
    else
        AutoStart::Disable(L"CameraMonitor");

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

    auto saveScript = [](const std::wstring& prefix,
        const std::vector<ScriptAction>& actions) {
            ConfigStore::SaveInt(prefix + L"Count", (int)actions.size());
            for (size_t i = 0; i < actions.size(); i++) {
                wchar_t idx[32];
                swprintf_s(idx, L"%zu_", i);
                std::wstring p = prefix + idx;
                ConfigStore::SaveInt(p + L"Type", (int)actions[i].type);
                ConfigStore::SaveString(p + L"Param1", actions[i].param1);
                ConfigStore::SaveString(p + L"Param2", actions[i].param2);
                ConfigStore::SaveInt(p + L"RepeatCount", actions[i].repeatCount);
                ConfigStore::SaveInt(p + L"Delay", actions[i].delay);
                ConfigStore::SaveInt(p + L"SoundWait", actions[i].soundWait);
            }
        };
    saveScript(L"ScriptStart_", g_scriptEngine.GetScript(ScriptEvent::CameraStart));
    saveScript(L"ScriptStop_", g_scriptEngine.GetScript(ScriptEvent::CameraStop));

    MessageBoxW(hDlg, L"设置已保存", L"提示", MB_OK | MB_ICONINFORMATION);
    EndDialog(hDlg, IDOK);
}

// ======================== 刷新摄像头 ========================
void SettingsDialog::OnRefreshCameras(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_CAM);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    IMFAttributes* pAttributes = nullptr;
    if (FAILED(MFCreateAttributes(&pAttributes, 1))) return;
    pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(pAttributes, &ppDevices, &count))) {
        for (UINT32 i = 0; i < count; i++) {
            WCHAR* name = nullptr;
            UINT32 nameLen = 0;
            if (SUCCEEDED(ppDevices[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen)))
            {
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)name);
                CoTaskMemFree(name);
            }
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    }
    pAttributes->Release();
}

// ======================== 脚本增删改 ========================
void SettingsDialog::OnAddScript(HWND hDlg, bool isStart)
{
    ScriptAction action;
    action.type = ScriptActionType::RunCmd;
    action.repeatCount = 1;
    action.soundWait = 0;

    if (!EditScriptAction(hDlg, action, true)) return;

    auto actions = g_scriptEngine.GetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop);
    actions.push_back(action);
    g_scriptEngine.SetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop, actions);
    LoadScriptsToList(hDlg);
}

void SettingsDialog::OnDelScript(HWND hDlg, bool isStart)
{
    HWND hList = GetDlgItem(hDlg, isStart ? IDC_LIST_START : IDC_LIST_STOP);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    auto actions = g_scriptEngine.GetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop);
    if (sel < (int)actions.size()) {
        actions.erase(actions.begin() + sel);
        g_scriptEngine.SetScript(
            isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop, actions);
        LoadScriptsToList(hDlg);
    }
}

void SettingsDialog::OnEditScript(HWND hDlg, bool isStart)
{
    HWND hList = GetDlgItem(hDlg, isStart ? IDC_LIST_START : IDC_LIST_STOP);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    auto actions = g_scriptEngine.GetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop);
    if (sel >= (int)actions.size()) return;

    if (!EditScriptAction(hDlg, actions[sel], false)) return;

    g_scriptEngine.SetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop, actions);
    LoadScriptsToList(hDlg);
}

void SettingsDialog::OnMoveScript(HWND hDlg, bool isStart, bool up)
{
    HWND hList = GetDlgItem(hDlg, isStart ? IDC_LIST_START : IDC_LIST_STOP);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    auto actions = g_scriptEngine.GetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop);

    if (up) {
        if (sel <= 0) return;
        std::swap(actions[sel], actions[sel - 1]);
        sel--;
    }
    else {
        if (sel >= (int)actions.size() - 1) return;
        std::swap(actions[sel], actions[sel + 1]);
        sel++;
    }

    g_scriptEngine.SetScript(
        isStart ? ScriptEvent::CameraStart : ScriptEvent::CameraStop, actions);
    LoadScriptsToList(hDlg);
    SendMessageW(hList, LB_SETCURSEL, sel, 0);
}

// ======================== 时间段增删 ========================
void SettingsDialog::OnAddRange(HWND hDlg)
{
    TimeRange r;
    r.startHour = GetDlgItemInt(hDlg, IDC_EDIT_STARTHH, NULL, FALSE);
    r.startMinute = GetDlgItemInt(hDlg, IDC_EDIT_STARTMM, NULL, FALSE);
    r.endHour = GetDlgItemInt(hDlg, IDC_EDIT_ENDHH, NULL, FALSE);
    r.endMinute = GetDlgItemInt(hDlg, IDC_EDIT_ENDMM, NULL, FALSE);
    r.enabled = true;

    int dayIds[] = { IDC_CHK_DAY_SUN, IDC_CHK_DAY_MON, IDC_CHK_DAY_TUE,
                     IDC_CHK_DAY_WED, IDC_CHK_DAY_THU, IDC_CHK_DAY_FRI,
                     IDC_CHK_DAY_SAT };
    for (int i = 0; i < 7; i++)
        r.days[i] = (IsDlgButtonChecked(hDlg, dayIds[i]) == BST_CHECKED);

    auto ranges = g_scheduleManager.GetRanges();
    ranges.push_back(r);
    g_scheduleManager.SetRanges(ranges);
    LoadRangesToList(hDlg);
}

void SettingsDialog::OnDelRange(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_TIMERANGE);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    auto ranges = g_scheduleManager.GetRanges();
    if (sel < (int)ranges.size()) {
        ranges.erase(ranges.begin() + sel);
        g_scheduleManager.SetRanges(ranges);
        LoadRangesToList(hDlg);
    }
}

// ======================== 键盘录制 ========================
std::wstring SettingsDialog::VkToName(DWORD vk)
{
    switch (vk) {
    case VK_RETURN:  return L"ENTER";
    case VK_ESCAPE:  return L"ESC";
    case VK_TAB:     return L"TAB";
    case VK_SPACE:   return L"SPACE";
    case VK_BACK:    return L"BACK";
    case VK_DELETE:  return L"DELETE";
    case VK_UP:      return L"UP";
    case VK_DOWN:    return L"DOWN";
    case VK_LEFT:    return L"LEFT";
    case VK_RIGHT:   return L"RIGHT";
    case VK_INSERT:  return L"INSERT";
    case VK_HOME:    return L"HOME";
    case VK_END:     return L"END";
    case VK_PRIOR:   return L"PGUP";
    case VK_NEXT:    return L"PGDN";
    case VK_CAPITAL: return L"CAPSLOCK";
    case VK_NUMLOCK: return L"NUMLOCK";
    case VK_SCROLL:  return L"SCROLLLOCK";
    case VK_SNAPSHOT:return L"PRINTSCREEN";
    case VK_PAUSE:   return L"PAUSE";
    case VK_APPS:    return L"APPS";
    case VK_F1:  return L"F1";
    case VK_F2:  return L"F2";
    case VK_F3:  return L"F3";
    case VK_F4:  return L"F4";
    case VK_F5:  return L"F5";
    case VK_F6:  return L"F6";
    case VK_F7:  return L"F7";
    case VK_F8:  return L"F8";
    case VK_F9:  return L"F9";
    case VK_F10: return L"F10";
    case VK_F11: return L"F11";
    case VK_F12: return L"F12";
    case VK_OEM_1: return L";";
    case VK_OEM_2: return L"/";
    case VK_OEM_3: return L"`";
    case VK_OEM_4: return L"[";
    case VK_OEM_5: return L"\\";
    case VK_OEM_6: return L"]";
    case VK_OEM_7: return L"'";
    case VK_OEM_PLUS:   return L"=";
    case VK_OEM_MINUS:  return L"-";
    case VK_OEM_COMMA:  return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_NUMPAD0: return L"NUM0";
    case VK_NUMPAD1: return L"NUM1";
    case VK_NUMPAD2: return L"NUM2";
    case VK_NUMPAD3: return L"NUM3";
    case VK_NUMPAD4: return L"NUM4";
    case VK_NUMPAD5: return L"NUM5";
    case VK_NUMPAD6: return L"NUM6";
    case VK_NUMPAD7: return L"NUM7";
    case VK_NUMPAD8: return L"NUM8";
    case VK_NUMPAD9: return L"NUM9";
    }
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
    return L"";
}

void SettingsDialog::StartKeyRecord(HWND hDlg)
{
    if (s_recording) {
        StopKeyRecord();
        return;
    }

    s_recordDlg = hDlg;
    s_recordBtn = GetDlgItem(hDlg, IDC_BTN_RECORD_KEY);
    s_recordedKey.clear();
    s_recording = true;
    SetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, L"请按下按键...");
    if (s_recordBtn) SetWindowTextW(s_recordBtn, L"停止录制");

    s_recordTimer = SetTimer(hDlg, RECORD_TIMER_ID, 30, NULL);
}

void SettingsDialog::StopKeyRecord()
{
    s_recording = false;
    if (s_recordTimer && s_recordDlg) {
        KillTimer(s_recordDlg, RECORD_TIMER_ID);
        s_recordTimer = 0;
    }
    s_recordDlg = nullptr;
    if (s_recordBtn) {
        SetWindowTextW(s_recordBtn, L"录制");
        s_recordBtn = nullptr;
    }
}

// ======================== 脚本编辑器 ========================
INT_PTR CALLBACK SettingsDialog::ScriptEditorProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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

        // 动作类型下拉框
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE);
        const wchar_t* types[] = {
            L"执行 CMD 命令", L"播放声音", L"模拟按键",
            L"弹出窗口", L"自定义通知", L"等待",
            L"重复执行", L"如果摄像头被占用", L"退出程序"
        };
        for (int i = 0; i < 9; i++)
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)types[i]);

        // 单键下拉框
        HWND hSingleKey = GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY);
        const wchar_t* singleKeys[] = {
    L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J",
    L"K", L"L", L"M", L"N", L"O", L"P", L"Q", L"R", L"S", L"T",
    L"U", L"V", L"W", L"X", L"Y", L"Z",
    L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9",
    L"F1", L"F2", L"F3", L"F4", L"F5", L"F6",
    L"F7", L"F8", L"F9", L"F10", L"F11", L"F12",
    L"ENTER", L"ESC", L"TAB", L"SPACE", L"BACK", L"DELETE",
    L"UP", L"DOWN", L"LEFT", L"RIGHT",
    L"INSERT", L"HOME", L"END", L"PGUP", L"PGDN",
    L"WIN", L"CAPSLOCK", L"NUMLOCK", L"SCROLLLOCK", L"PRINTSCREEN",
    L"NUM0", L"NUM1", L"NUM2", L"NUM3", L"NUM4",
    L"NUM5", L"NUM6", L"NUM7", L"NUM8", L"NUM9"
        };
        for (int i = 0; i < _countof(singleKeys); i++)
            SendMessageW(hSingleKey, CB_ADDSTRING, 0, (LPARAM)singleKeys[i]);
        SendMessageW(hSingleKey, CB_SETCURSEL, 0, 0);

        // 播放方式下拉框
        HWND hSoundWait = GetDlgItem(hDlg, IDC_COMBO_SOUNDWAIT);
        SendMessageW(hSoundWait, CB_ADDSTRING, 0, (LPARAM)L"不等待（后台播放）");
        SendMessageW(hSoundWait, CB_ADDSTRING, 0, (LPARAM)L"等待播放完毕");
        SendMessageW(hSoundWait, CB_SETCURSEL, 0, 0);

        if (s_editingAction) {
            SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)(int)s_editingAction->type, 0);
            SetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, s_editingAction->param1.c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_PARAM2, s_editingAction->param2.c_str());
            SetDlgItemInt(hDlg, IDC_EDIT_REPEAT, s_editingAction->repeatCount, FALSE);
            SendMessageW(hSoundWait, CB_SETCURSEL,
                s_editingAction->soundWait ? 1 : 0, 0);

            // 如果 param1 是单个键，尝试在下拉框中选中
            if (s_editingAction->type == ScriptActionType::SimulateKey) {
                int idx = (int)SendMessageW(hSingleKey, CB_FINDSTRINGEXACT,
                    (WPARAM)-1, (LPARAM)s_editingAction->param1.c_str());
                if (idx != CB_ERR)
                    SendMessageW(hSingleKey, CB_SETCURSEL, idx, 0);
            }
        }
        else {
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
            SetDlgItemInt(hDlg, IDC_EDIT_REPEAT, 1, FALSE);
        }

        int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
        SetDlgItemTextW(hDlg, IDC_STATIC_HINT, ActionHint((ScriptActionType)sel));

        bool needSoundWait = (sel == (int)ScriptActionType::PlaySoundAction);
        EnableWindow(GetDlgItem(hDlg, IDC_COMBO_SOUNDWAIT), needSoundWait);
        EnableWindow(GetDlgItem(hDlg, IDC_STATIC_SOUNDWAIT), needSoundWait);

        bool needRecord = (sel == (int)ScriptActionType::SimulateKey);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_RECORD_KEY), needRecord);

        bool needSingleKeyInit = (sel == (int)ScriptActionType::SimulateKey);
        EnableWindow(GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY), needSingleKeyInit);
        EnableWindow(GetDlgItem(hDlg, IDC_STATIC_SINGLEKEY), needSingleKeyInit);

        HICON hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    return TRUE;

    case WM_TIMER:
        if (wParam == RECORD_TIMER_ID && s_recording) {
            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool win = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) ||
                ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

            DWORD pressedVk = 0;
            for (DWORD vk = 0x08; vk <= 0xFE; vk++) {
                if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                    vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                    vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                    vk == VK_LWIN || vk == VK_RWIN) continue;

                if (GetAsyncKeyState(vk) & 0x8000) {
                    pressedVk = vk;
                    break;
                }
            }

            if (pressedVk != 0) {
                std::wstring combo;
                if (ctrl)  combo += L"CTRL+";
                if (alt)   combo += L"ALT+";
                if (shift) combo += L"SHIFT+";
                if (win)   combo += L"WIN+";

                std::wstring keyName = VkToName(pressedVk);
                if (keyName.empty()) {
                    wchar_t buf[32];
                    swprintf_s(buf, L"VK_%02X", pressedVk);
                    keyName = buf;
                }
                combo += keyName;

                s_recordedKey = combo;
                StopKeyRecord();
                SetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, s_recordedKey.c_str());
            }
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_COMBO_ACTION_TYPE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                ScriptActionType t = (ScriptActionType)sel;

                SetDlgItemTextW(hDlg, IDC_STATIC_HINT, ActionHint(t));

                bool needParam1 = !(t == ScriptActionType::ExitProgram);
                bool needParam2 = (t == ScriptActionType::CreateWindow ||
                    t == ScriptActionType::CustomNotify);
                bool needRepeat = (t == ScriptActionType::Repeat);
                bool needSoundWait = (t == ScriptActionType::PlaySoundAction);
                bool needRecord = (t == ScriptActionType::SimulateKey);
                bool needSingleKey = (t == ScriptActionType::SimulateKey);

                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PARAM1), needParam1);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PARAM2), needParam2);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_REPEAT), needRepeat);
                EnableWindow(GetDlgItem(hDlg, IDC_STATIC_PARAM1), needParam1);
                EnableWindow(GetDlgItem(hDlg, IDC_STATIC_PARAM2), needParam2);
                EnableWindow(GetDlgItem(hDlg, IDC_STATIC_REPEAT), needRepeat);
                EnableWindow(GetDlgItem(hDlg, IDC_COMBO_SOUNDWAIT), needSoundWait);
                EnableWindow(GetDlgItem(hDlg, IDC_STATIC_SOUNDWAIT), needSoundWait);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_RECORD_KEY), needRecord);
                EnableWindow(GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY), needSingleKey);
                EnableWindow(GetDlgItem(hDlg, IDC_STATIC_SINGLEKEY), needSingleKey);
            }
            break;

        case IDC_COMBO_SINGLE_KEY:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND hSingleKey = GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY);
                int idx = (int)SendMessageW(hSingleKey, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    wchar_t buf[64];
                    SendMessageW(hSingleKey, CB_GETLBTEXT, idx, (LPARAM)buf);
                    SetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, buf);
                }
            }
            break;

        case IDC_BTN_RECORD_KEY:
            StartKeyRecord(hDlg);
            break;

        case IDC_BTN_OK:
        {
            if (s_editingAction) {
                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                s_editingAction->type = (ScriptActionType)sel;

                wchar_t buf[512];
                GetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, buf, 512);
                s_editingAction->param1 = buf;
                GetDlgItemTextW(hDlg, IDC_EDIT_PARAM2, buf, 512);
                s_editingAction->param2 = buf;
                s_editingAction->repeatCount = GetDlgItemInt(hDlg, IDC_EDIT_REPEAT, NULL, FALSE);
                s_editingAction->soundWait = (int)SendMessageW(
                    GetDlgItem(hDlg, IDC_COMBO_SOUNDWAIT), CB_GETCURSEL, 0, 0);
            }
            StopKeyRecord();
            EndDialog(hDlg, IDOK);
        }
        break;

        case IDC_BTN_CANCEL_SCRIPT:
            StopKeyRecord();
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return TRUE;

    case WM_CLOSE:
        StopKeyRecord();
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

bool SettingsDialog::EditScriptAction(HWND parent, ScriptAction& action, bool isNew)
{
    s_editingAction = &action;
    INT_PTR ret = DialogBoxParamW(GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_SCRIPT_EDITOR), parent, ScriptEditorProc, 0);
    s_editingAction = nullptr;
    return ret == IDOK;
}

// ======================== 对话框过程 ========================
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
        SetWindowPos(hDlg, NULL, (sw - w) / 2, (sh - h) / 2, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);

        HICON hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    return TRUE;

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_TAB && pnmh->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(s_hTab);
            OnTabChanged(hDlg, sel);
        }
    }
    return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_REFRESH:    OnRefreshCameras(hDlg); break;
        case IDC_BTN_ADD_START:  OnAddScript(hDlg, true); break;
        case IDC_BTN_DEL_START:  OnDelScript(hDlg, true); break;
        case IDC_BTN_EDIT_START: OnEditScript(hDlg, true); break;
        case IDC_BTN_ADD_STOP:   OnAddScript(hDlg, false); break;
        case IDC_BTN_DEL_STOP:   OnDelScript(hDlg, false); break;
        case IDC_BTN_EDIT_STOP:  OnEditScript(hDlg, false); break;
        case IDC_BTN_UP_START:    OnMoveScript(hDlg, true, true); break;
        case IDC_BTN_DOWN_START:  OnMoveScript(hDlg, true, false); break;
        case IDC_BTN_UP_STOP:     OnMoveScript(hDlg, false, true); break;
        case IDC_BTN_DOWN_STOP:   OnMoveScript(hDlg, false, false); break;
        case IDC_BTN_ADD_RANGE:  OnAddRange(hDlg); break;
        case IDC_BTN_DEL_RANGE:  OnDelRange(hDlg); break;
        case IDC_BTN_SAVE:       OnSave(hDlg); break;
        case IDC_BTN_CANCEL:     EndDialog(hDlg, IDCANCEL); break;
        }
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// ======================== 显示 ========================
INT_PTR SettingsDialog::Show(HINSTANCE hInst, HWND parent)
{
    return DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG),
        NULL, DlgProc, 0);
}