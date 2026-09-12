#include "ScriptEngine.h"
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

extern NOTIFYICONDATAW g_nid;
extern HWND            g_hwnd;

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
        ExecuteAction(action);
    }
}

void ScriptEngine::ExecuteAction(const ScriptAction& action)
{
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
        Sleep(action.delay);
        break;

    case ScriptActionType::Repeat:
        for (int i = 0; i < action.repeatCount; i++) {
            ExecuteActions(action.children);
        }
        break;

    case ScriptActionType::IfCameraInUse:
        ExecuteActions(action.children);
        break;

    case ScriptActionType::ExitProgram:
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        break;
    }
}

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

    if (wait) {
        // 等待播放完毕：SND_SYNC 会阻塞直到声音播放结束
        PlaySoundW(path.c_str(), NULL, SND_FILENAME | SND_SYNC);
    }
    else {
        // 不等待：SND_ASYNC 立即返回，声音在后台播放
        PlaySoundW(path.c_str(), NULL, SND_FILENAME | SND_ASYNC);
    }
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