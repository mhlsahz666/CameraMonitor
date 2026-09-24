#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <string>

class Logger {
public:
    // 初始化（从配置里读开关和路径）
    static void Init(bool enabled, const std::wstring& path);

    // 日志写入
    static void Write(const std::wstring& message);

    // 便捷函数
    static void ProgramStart();
    static void ProgramStop();
    static void CameraOccupied(const std::wstring& deviceName,
        const std::wstring& processName,
        DWORD pid);
    static void CameraReleased(const std::wstring& deviceName);

    static bool IsEnabled();
    static std::wstring GetPath();

private:
    static bool s_enabled;
    static std::wstring s_path;
    static CRITICAL_SECTION s_cs;
    static bool s_initialized;

    static std::wstring GetTimestamp();
};