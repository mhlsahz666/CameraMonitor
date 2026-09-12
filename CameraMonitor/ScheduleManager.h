#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <vector>

struct TimeRange {
    int startHour = 8;
    int startMinute = 0;
    int endHour = 22;
    int endMinute = 0;
    bool enabled = true;
    bool days[7] = { true, true, true, true, true, true, true };
};

class ScheduleManager {
public:
    ScheduleManager();
    ~ScheduleManager();

    void SetRanges(const std::vector<TimeRange>& ranges);
    std::vector<TimeRange> GetRanges() const;
    void Start(HWND notifyWnd);
    void Stop();
    bool IsInAllowedTime() const;

private:
    std::vector<TimeRange> m_ranges;
    HWND m_notifyWnd = nullptr;
    HANDLE m_timerThread = nullptr;
    HANDLE m_stopEvent = nullptr;

    static DWORD WINAPI TimerThreadProc(LPVOID param);
    void CheckTime();
};