#include "Logger.h"
#include <cstdio>

bool Logger::s_enabled = false;
std::wstring Logger::s_path;
CRITICAL_SECTION Logger::s_cs;
bool Logger::s_initialized = false;

void Logger::Init(bool enabled, const std::wstring& path)
{
    if (!s_initialized) {
        InitializeCriticalSection(&s_cs);
        s_initialized = true;
    }
    s_enabled = enabled;
    s_path = path;
}

bool Logger::IsEnabled()
{
    return s_enabled;
}

std::wstring Logger::GetPath()
{
    return s_path;
}

std::wstring Logger::GetTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"[%04d-%02d-%02d %02d:%02d:%02d]",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return buf;
}

void Logger::Write(const std::wstring& message)
{
    if (!s_enabled || s_path.empty()) return;

    EnterCriticalSection(&s_cs);

    FILE* f = nullptr;
    if (_wfopen_s(&f, s_path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        std::wstring line = GetTimestamp() + L" " + message + L"\n";
        fputws(line.c_str(), f);
        fclose(f);
    }

    LeaveCriticalSection(&s_cs);
}

void Logger::ProgramStart()
{
    Write(L"程序启动");
}

void Logger::ProgramStop()
{
    Write(L"程序退出");
}

void Logger::CameraOccupied(const std::wstring& deviceName,
    const std::wstring& processName,
    DWORD pid)
{
    wchar_t buf[512];
    swprintf_s(buf, L"摄像头占用 - 设备：%s，程序：%s (PID: %lu)",
        deviceName.c_str(), processName.c_str(), pid);
    Write(buf);
}

void Logger::CameraReleased(const std::wstring& deviceName)
{
    wchar_t buf[512];
    swprintf_s(buf, L"摄像头停止占用 - 设备：%s", deviceName.c_str());
    Write(buf);
}