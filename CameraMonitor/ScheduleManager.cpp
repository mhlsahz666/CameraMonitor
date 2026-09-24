#include "ScheduleManager.h"
#include <ctime>

#define WM_SCHEDULE_EXIT (WM_USER + 10)

ScheduleManager::ScheduleManager() {}
ScheduleManager::~ScheduleManager() { Stop(); }

void ScheduleManager::SetRanges(const std::vector<TimeRange>& ranges)
{
    m_ranges = ranges;
}

std::vector<TimeRange> ScheduleManager::GetRanges() const
{
    return m_ranges;
}

void ScheduleManager::Start(HWND notifyWnd)
{
    m_notifyWnd = notifyWnd;
    m_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    m_timerThread = CreateThread(NULL, 0, TimerThreadProc, this, 0, NULL);
}

void ScheduleManager::Stop()
{
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_timerThread) {
        WaitForSingleObject(m_timerThread, 3000);
        CloseHandle(m_timerThread);
        m_timerThread = nullptr;
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

DWORD WINAPI ScheduleManager::TimerThreadProc(LPVOID param)
{
    ScheduleManager* self = (ScheduleManager*)param;
    while (WaitForSingleObject(self->m_stopEvent, 30000) == WAIT_TIMEOUT) {
        self->CheckTime();
    }
    return 0;
}

void ScheduleManager::CheckTime()
{
    if (!IsInAllowedTime()) {
        PostMessage(m_notifyWnd, WM_SCHEDULE_EXIT, 0, 0);
    }
}

bool ScheduleManager::IsInAllowedTime() const
{
    for (const auto& r : m_ranges) {
        if (!r.enabled) continue;
        // 只要有一条启用，就认为需要检查
        goto HAS_ENABLED;
    }
    return true;

HAS_ENABLED:
    time_t now = time(nullptr);
    tm local;
    localtime_s(&local, &now);
    int curMinutes = local.tm_hour * 60 + local.tm_min;
    int curDay = local.tm_wday;

    for (const auto& r : m_ranges) {
        if (!r.enabled) continue;
        if (!r.days[curDay]) continue;

        int startMin = r.startHour * 60 + r.startMinute;
        int endMin = r.endHour * 60 + r.endMinute;

        bool inRange = false;
        if (startMin <= endMin) {
            inRange = (curMinutes >= startMin && curMinutes < endMin);
        }
        else {
            inRange = (curMinutes >= startMin || curMinutes < endMin);
        }
        if (inRange) return false;
    }
    return true;
}