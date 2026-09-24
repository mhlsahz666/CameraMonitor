#include "ScriptEngine.h"
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <sstream>
#include <thread>
#include <chrono>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

extern NOTIFYICONDATAW g_nid;
extern HWND            g_hwnd;

// 由 main.cpp 维护：当前占用摄像头的进程名
extern std::wstring g_currentOccupiedProcess;
extern std::wstring g_currentOccupiedCamera;
extern bool g_cameraInUse;

ScriptEngine::ScriptEngine()
{
    InitializeCriticalSection(&m_cs);
}

ScriptEngine::~ScriptEngine()
{
    DeleteCriticalSection(&m_cs);
}

void ScriptEngine::SetScript(ScriptEvent evt, const std::vector<ScriptAction>& actions)
{
    EnterCriticalSection(&m_cs);
    if (evt == ScriptEvent::CameraStart)
        m_startScript = actions;
    else
        m_stopScript = actions;
    LeaveCriticalSection(&m_cs);
}

std::vector<ScriptAction> ScriptEngine::GetScript(ScriptEvent evt) const
{
    EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&m_cs));
    auto result = (evt == ScriptEvent::CameraStart) ? m_startScript : m_stopScript;
    LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&m_cs));
    return result;
}

void ScriptEngine::TriggerEvent(ScriptEvent evt)
{
    EnterCriticalSection(&m_cs);
    auto actions = (evt == ScriptEvent::CameraStart) ? m_startScript : m_stopScript;
    LeaveCriticalSection(&m_cs);

    std::thread([this, actions]() {
        ExecuteActions(actions);
        }).detach();
}

void ScriptEngine::ExecuteActions(const std::vector<ScriptAction>& actions)
{
    for (const auto& action : actions) {
        if (!action.enabled) continue;
        ExecuteAction(action);
    }
}

// ======================== 条件求值（用于 BlockIf）========================
static bool EvaluateCondition(const std::wstring& left, LogicalOp op,
    const std::wstring& right)
{
    switch (op) {
    case LogicalOp::Equal:
        return left == right;
    case LogicalOp::NotEqual:
        return left != right;
    case LogicalOp::Greater: {
        int a = _wtoi(left.c_str());
        int b = _wtoi(right.c_str());
        return a > b;
    }
    case LogicalOp::Less: {
        int a = _wtoi(left.c_str());
        int b = _wtoi(right.c_str());
        return a < b;
    }
    case LogicalOp::Contains:
        return left.find(right) != std::wstring::npos;
    }
    return false;
}

// ======================== 执行动作 ========================
void ScriptEngine::ExecuteAction(const ScriptAction& action)
{
    if (!action.enabled) return;

    switch (action.type)
    {
    case ScriptActionType::RunCmd:
        ExecuteRunCmd(action.param1);
        break;

    case ScriptActionType::PlaySoundAction:
        ExecutePlaySound(action.param1, action.soundWait != 0);
        break;

    case ScriptActionType::SimulateKey:
        ExecuteSimulateKey(action.param1);
        break;

    case ScriptActionType::CreateWindow:
        ExecuteCreateWindow(action.param1, action.param2);
        break;

    case ScriptActionType::CustomNotify:
        ExecuteCustomNotify(action.param1, action.param2);
        break;

    case ScriptActionType::Wait:
    {
        int ms = _wtoi(action.param1.c_str());
        if (ms > 0) Sleep(ms);
    }
    break;

    case ScriptActionType::RandomNumber:
        ExecuteRandomNumber(action);
        break;

    case ScriptActionType::ExitProgram:
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        break;

        // ==================== 块 ====================
    case ScriptActionType::BlockEventCameraStart:
    case ScriptActionType::BlockEventCameraStop:
        // 事件块本身就是顶层触发，直接执行子动作
        ExecuteActions(action.children);
        break;

    case ScriptActionType::BlockIfCameraOccupied:
    {
        // param1 为空 → 只要有摄像头被占用就执行
        // param1 非空 → 用“包含”匹配 g_currentOccupiedCamera
        if (g_cameraInUse) {
            if (action.param1.empty() ||
                g_currentOccupiedCamera.find(action.param1) != std::wstring::npos) {
                ExecuteActions(action.children);
            }
        }
    }
    break;

    case ScriptActionType::BlockIfProcessOccupied:
        // 如果 xx 程序占用：比较 g_currentOccupiedProcess 和 param1
    {
        if (EvaluateCondition(g_currentOccupiedProcess,
            LogicalOp::Contains, action.param1)) {
            ExecuteActions(action.children);
        }
    }
    break;

    case ScriptActionType::BlockIfRandom:
    {
        int r = rand() % 100 + 1;
        if (r > 50) ExecuteActions(action.children);
    }
    break;

    case ScriptActionType::BlockRepeat:
    {
        int count = action.repeatCount > 0 ? action.repeatCount : 1;
        for (int i = 0; i < count; i++)
            ExecuteActions(action.children);
    }
    break;

    case ScriptActionType::BlockIf:
    {
        // param1 = 变量名，param2 = 比较值
        // 变量名约定："进程名"、"摄像头名"、"随机数"
        std::wstring leftValue;
        if (action.param1 == L"进程名") {
            leftValue = g_currentOccupiedProcess;
        }
        else if (action.param1 == L"摄像头名") {
            leftValue = L"";  // TODO：如需摄像头名，扩展全局变量
        }
        else if (action.param1 == L"随机数") {
            wchar_t buf[16];
            swprintf_s(buf, L"%d", rand() % 100);
            leftValue = buf;
        }
        else {
            leftValue = action.param1;  // 直接当字面量
        }

        if (EvaluateCondition(leftValue, action.logicalOp, action.param2)) {
            ExecuteActions(action.children);
        }
    }
    break;
    }
}

// ======================== 普通动作实现 ========================
void ScriptEngine::ExecuteRunCmd(const std::wstring& cmd)
{
    if (cmd.empty()) return;

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::wstring cmdLine = L"cmd.exe /c " + cmd;
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);

    if (CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void ScriptEngine::ExecutePlaySound(const std::wstring& path, bool wait)
{
    if (path.empty()) return;
    if (wait)
        PlaySoundW(path.c_str(), NULL, SND_FILENAME | SND_SYNC);
    else
        PlaySoundW(path.c_str(), NULL, SND_FILENAME | SND_ASYNC);
}

void ScriptEngine::ExecuteSimulateKey(const std::wstring& key)
{
    if (key.empty()) return;

    std::vector<WORD> keys;
    std::wstring token;
    std::wstringstream ss(key);

    while (std::getline(ss, token, L'+')) {
        while (!token.empty() && token.front() == L' ') token.erase(0, 1);
        while (!token.empty() && token.back() == L' ') token.pop_back();

        if (token == L"CTRL")       keys.push_back(VK_CONTROL);
        else if (token == L"ALT")   keys.push_back(VK_MENU);
        else if (token == L"SHIFT") keys.push_back(VK_SHIFT);
        else if (token == L"WIN")   keys.push_back(VK_LWIN);
        else if (token.size() == 1) keys.push_back((WORD)toupper(token[0]));
        else if (token == L"F1")    keys.push_back(VK_F1);
        else if (token == L"F2")    keys.push_back(VK_F2);
        else if (token == L"F3")    keys.push_back(VK_F3);
        else if (token == L"F4")    keys.push_back(VK_F4);
        else if (token == L"F5")    keys.push_back(VK_F5);
        else if (token == L"F6")    keys.push_back(VK_F6);
        else if (token == L"F7")    keys.push_back(VK_F7);
        else if (token == L"F8")    keys.push_back(VK_F8);
        else if (token == L"F9")    keys.push_back(VK_F9);
        else if (token == L"F10")   keys.push_back(VK_F10);
        else if (token == L"F11")   keys.push_back(VK_F11);
        else if (token == L"F12")   keys.push_back(VK_F12);
        else if (token == L"ENTER") keys.push_back(VK_RETURN);
        else if (token == L"ESC")   keys.push_back(VK_ESCAPE);
        else if (token == L"TAB")   keys.push_back(VK_TAB);
        else if (token == L"SPACE") keys.push_back(VK_SPACE);
        else if (token == L"BACK")  keys.push_back(VK_BACK);
        else if (token == L"DELETE") keys.push_back(VK_DELETE);
        else if (token == L"UP")    keys.push_back(VK_UP);
        else if (token == L"DOWN")  keys.push_back(VK_DOWN);
        else if (token == L"LEFT")  keys.push_back(VK_LEFT);
        else if (token == L"RIGHT") keys.push_back(VK_RIGHT);
        else if (token == L"INSERT") keys.push_back(VK_INSERT);
        else if (token == L"HOME")  keys.push_back(VK_HOME);
        else if (token == L"END")   keys.push_back(VK_END);
        else if (token == L"PGUP")  keys.push_back(VK_PRIOR);
        else if (token == L"PGDN")  keys.push_back(VK_NEXT);
        else if (token == L"CAPSLOCK") keys.push_back(VK_CAPITAL);
        else if (token == L"NUMLOCK") keys.push_back(VK_NUMLOCK);
        else if (token == L"SCROLLLOCK") keys.push_back(VK_SCROLL);
        else if (token == L"PRINTSCREEN") keys.push_back(VK_SNAPSHOT);
        else if (token == L"NUM0") keys.push_back(VK_NUMPAD0);
        else if (token == L"NUM1") keys.push_back(VK_NUMPAD1);
        else if (token == L"NUM2") keys.push_back(VK_NUMPAD2);
        else if (token == L"NUM3") keys.push_back(VK_NUMPAD3);
        else if (token == L"NUM4") keys.push_back(VK_NUMPAD4);
        else if (token == L"NUM5") keys.push_back(VK_NUMPAD5);
        else if (token == L"NUM6") keys.push_back(VK_NUMPAD6);
        else if (token == L"NUM7") keys.push_back(VK_NUMPAD7);
        else if (token == L"NUM8") keys.push_back(VK_NUMPAD8);
        else if (token == L"NUM9") keys.push_back(VK_NUMPAD9);
        else if (token.size() == 6 && token.substr(0, 3) == L"VK_")
            keys.push_back((WORD)wcstoul(token.substr(3).c_str(), nullptr, 16));
    }

    if (keys.empty()) return;

    for (auto k : keys) keybd_event((BYTE)k, 0, 0, 0);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        keybd_event((BYTE)*it, 0, KEYEVENTF_KEYUP, 0);
}

void ScriptEngine::ExecuteCreateWindow(const std::wstring& title, const std::wstring& text)
{
    MessageBoxW(NULL, text.c_str(), title.c_str(), MB_OK | MB_TOPMOST);
}

void ScriptEngine::ExecuteCustomNotify(const std::wstring& title, const std::wstring& text)
{
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title.c_str());
    wcscpy_s(g_nid.szInfo, text.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = 0;
}

void ScriptEngine::ExecuteRandomNumber(const ScriptAction& action)
{
    int r = rand() % (action.randomMax - action.randomMin + 1) + action.randomMin;
    wchar_t buf[64];
    swprintf_s(buf, L"随机数：%d", r);
    ExecuteCustomNotify(L"随机数", buf);
}