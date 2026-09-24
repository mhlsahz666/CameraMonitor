#pragma once
#include <windows.h>
#include "resource.h"

class TimeRangeEditor {
public:
    static INT_PTR Show(HINSTANCE hInst, HWND parent);  // 新建
private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static void OnInit(HWND hDlg);
};