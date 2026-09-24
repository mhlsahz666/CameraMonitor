#include "TimeRangeIO.h"
#include <fstream>
#include <sstream>

// 星期映射
static const wchar_t* DayNames[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };

// ======================== 判断文件头 ========================
bool TimeRangeIO::IsTimeRangeFile(const std::wstring& path)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) return false;

    wchar_t line[256] = {};
    fgetws(line, 256, f);
    fclose(f);

    std::wstring s = line;
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();

    return s == TR_FILE_HEADER;
}

// ======================== 导出 ========================
bool TimeRangeIO::ExportToFile(const std::wstring& path,
    const std::vector<TimeRange>& ranges,
    std::wstring& error)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) {
        error = L"无法创建文件：" + path;
        return false;
    }

    // 写文件头
    fwprintf(f, L"%s\n", TR_FILE_HEADER);
    fwprintf(f, L"{\n");

    for (const auto& r : ranges) {
        // 星期
        std::wstring days;
        for (int d = 0; d < 7; d++) {
            if (r.days[d]) {
                if (!days.empty()) days += L",";
                days += DayNames[d];
            }
        }
        if (days.empty()) days = L"无";

        fwprintf(f, L"    %02d:%02d-%02d:%02d [%s] %s\n",
            r.startHour, r.startMinute, r.endHour, r.endMinute,
            days.c_str(),
            r.enabled ? L"启用" : L"禁用");
    }

    fwprintf(f, L"}\n");
    fclose(f);

    return true;
}

// ======================== 导入 ========================
bool TimeRangeIO::ImportFromFile(const std::wstring& path,
    std::vector<TimeRange>& ranges,
    std::wstring& error)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) {
        error = L"无法打开文件：" + path;
        return false;
    }

    // 检查文件头
    wchar_t line[512] = {};
    if (!fgetws(line, 512, f)) {
        fclose(f);
        error = L"文件为空";
        return false;
    }
    std::wstring header = line;
    while (!header.empty() && (header.back() == L'\n' || header.back() == L'\r'))
        header.pop_back();
    if (header != TR_FILE_HEADER) {
        fclose(f);
        error = L"文件类型错误，不是时间段文件";
        return false;
    }

    std::vector<TimeRange> newRanges;

    while (fgetws(line, 512, f)) {
        std::wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();

        // 跳过空行、{
        size_t p = s.find_first_not_of(L" \t");
        if (p == std::wstring::npos) continue;
        s = s.substr(p);
        if (s.empty() || s[0] == L'{' || s[0] == L'}') continue;

        // 解析 "08:00-09:00 [一,二,三] 启用"
        size_t dash = s.find(L'-');
        size_t bracket1 = s.find(L'[');
        size_t bracket2 = s.find(L']');
        if (dash == std::wstring::npos || bracket1 == std::wstring::npos) continue;

        std::wstring startStr = s.substr(0, dash);
        std::wstring endStr = s.substr(dash + 1, bracket1 - dash - 1);
        std::wstring daysStr = s.substr(bracket1 + 1, bracket2 - bracket1 - 1);
        std::wstring tailStr = (bracket2 == std::wstring::npos) ?
            L"" : s.substr(bracket2 + 1);

        // 去掉两端空格
        auto trim = [](std::wstring& str) {
            while (!str.empty() && str.front() == L' ') str.erase(0, 1);
            while (!str.empty() && str.back() == L' ') str.pop_back();
            };
        trim(startStr); trim(endStr); trim(daysStr); trim(tailStr);

        TimeRange r;
        int sh = 0, sm = 0, eh = 0, em = 0;
        swscanf_s(startStr.c_str(), L"%d:%d", &sh, &sm);
        swscanf_s(endStr.c_str(), L"%d:%d", &eh, &em);
        r.startHour = sh; r.startMinute = sm;
        r.endHour = eh; r.endMinute = em;

        for (int d = 0; d < 7; d++) r.days[d] = false;
        for (int d = 0; d < 7; d++) {
            if (daysStr.find(DayNames[d]) != std::wstring::npos)
                r.days[d] = true;
        }

        r.enabled = (tailStr.find(L"启用") != std::wstring::npos);

        newRanges.push_back(r);
    }
    fclose(f);

    if (newRanges.empty()) {
        error = L"文件中没有有效的时间段";
        return false;
    }

    ranges = newRanges;
    return true;
}