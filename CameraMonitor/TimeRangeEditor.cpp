#include "TimeRangeEditor.h"
#include "ScheduleManager.h"
#include <commctrl.h>

extern ScheduleManager g_scheduleManager;

INT_PTR TimeRangeEditor::Show(HINSTANCE hInst, HWND parent)
{
    return DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_TIMERANGE_EDITOR), parent, DlgProc, 0);
}

void TimeRangeEditor::OnInit(HWND hDlg)
{
    HWND hDay = GetDlgItem(hDlg, IDC_COMBO_DAY);
    const wchar_t* days[] = { L"周一", L"周二", L"周三", L"周四", L"周五", L"周六", L"周日" };
    for (int i = 0; i < 7; i++) SendMessageW(hDay, CB_ADDSTRING, 0, (LPARAM)days[i]);
    SendMessageW(hDay, CB_SETCURSEL, 0, 0);

    HWND hHour = GetDlgItem(hDlg, IDC_COMBO_HOUR);
    for (int i = 0; i < 24; i++) {
        wchar_t buf[4]; swprintf_s(buf, L"%02d", i);
        SendMessageW(hHour, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessageW(hHour, CB_SETCURSEL, 8, 0);

    HWND hMin = GetDlgItem(hDlg, IDC_COMBO_MIN);
    for (int i = 0; i < 60; i += 5) {
        wchar_t buf[4]; swprintf_s(buf, L"%02d", i);
        SendMessageW(hMin, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessageW(hMin, CB_SETCURSEL, 0, 0);

    HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    EnumChildWindows(hDlg, [](HWND hChild, LPARAM lp) -> BOOL {
        SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
        }, (LPARAM)hFont);
}

INT_PTR CALLBACK TimeRangeEditor::DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        OnInit(hDlg);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_TR_SAVE) {
            HWND hDay = GetDlgItem(hDlg, IDC_COMBO_DAY);
            HWND hHour = GetDlgItem(hDlg, IDC_COMBO_HOUR);
            HWND hMin = GetDlgItem(hDlg, IDC_COMBO_MIN);

            int day = (int)SendMessageW(hDay, CB_GETCURSEL, 0, 0);
            int hour = (int)SendMessageW(hHour, CB_GETCURSEL, 0, 0);
            int min = (int)SendMessageW(hMin, CB_GETCURSEL, 0, 0) * 5;

            TimeRange r;
            r.startHour = hour;
            r.startMinute = min;
            r.endHour = hour;
            r.endMinute = min + 30;
            if (r.endMinute >= 60) { r.endMinute -= 60; r.endHour++; }
            if (r.endHour >= 24) r.endHour = 0;
            r.enabled = true;
            for (int d = 0; d < 7; d++) r.days[d] = false;
            r.days[day] = true;

            auto ranges = g_scheduleManager.GetRanges();
            ranges.push_back(r);
            g_scheduleManager.SetRanges(ranges);

            EndDialog(hDlg, IDOK);
        }
        else if (LOWORD(wParam) == IDC_BTN_TR_CANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}