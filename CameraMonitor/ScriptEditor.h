#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "ScriptEngine.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

class ScriptEditor {
public:
    static bool Show(HINSTANCE hInst, HWND parent,
        std::vector<ScriptAction>& actions,
        const std::wstring& scriptName, bool isNew);

private:
    static std::vector<ScriptAction>* s_actions;
    static std::wstring s_scriptName;
    static bool s_isNew;

    // 每一行对应的动作路径
    static std::vector<std::vector<int>> s_linePaths;

    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static void OnInit(HWND hDlg);
    static void LoadCommands(HWND hDlg);
    static void OnAddCommand(HWND hDlg);
    static void OnEditCommand(HWND hDlg);
    static void OnDelCommand(HWND hDlg);
    static void OnMoveCommand(HWND hDlg, bool up);
    static void OnMoveIntoBlock(HWND hDlg);
    static void OnMoveOutOfBlock(HWND hDlg);
    static void OnSave(HWND hDlg);

    // 大括号渲染
    static void AddIndent(std::wstring& text, int level);
    static std::wstring RenderAction(const ScriptAction& a, int level);

    // 路径访问工具
    static ScriptAction* GetActionByPath(std::vector<int> path);
    static bool RemoveActionByPath(std::vector<int> path);
    static void InsertActionIntoBlock(std::vector<int> blockPath, const ScriptAction& action);
};