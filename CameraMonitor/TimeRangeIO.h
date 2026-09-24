#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <string>
#include <vector>
#include "ScheduleManager.h"

// 时间段文件头
#define TR_FILE_HEADER  L"[CAMMON_TIMERANGE]"

class TimeRangeIO {
public:
    // 导出到文件
    static bool ExportToFile(const std::wstring& path,
        const std::vector<TimeRange>& ranges,
        std::wstring& error);

    // 从文件导入（会替换传入的 ranges）
    static bool ImportFromFile(const std::wstring& path,
        std::vector<TimeRange>& ranges,
        std::wstring& error);

    // 判断文件是否为时间段文件
    static bool IsTimeRangeFile(const std::wstring& path);
};