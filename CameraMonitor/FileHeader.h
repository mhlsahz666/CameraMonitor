#pragma once
#include <windows.h>
#include <string>

// 文件头标记
#define HEADER_SCRIPT      L"[CAMMON_SCRIPT]"
#define HEADER_TIMERANGE   L"[CAMMON_TIMERANGE]"
#define HEADER_CONFIG      L"[CAMMON_CONFIG]"

// 文件后缀
#define EXT_SCRIPT         L"cms"
#define EXT_TIMERANGE      L"cmt"
#define EXT_CONFIG         L"cmc"

enum class FileType { Unknown, Script, TimeRange, Config };

inline FileType DetectFileType(const std::wstring& path)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
        return FileType::Unknown;

    wchar_t line[256] = {};
    fgetws(line, 256, f);
    fclose(f);

    std::wstring s = line;
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' '))
        s.pop_back();

    if (s == HEADER_SCRIPT)    return FileType::Script;
    if (s == HEADER_TIMERANGE) return FileType::TimeRange;
    if (s == HEADER_CONFIG)    return FileType::Config;
    return FileType::Unknown;
}

inline bool CheckFileHeader(HWND hDlg, const std::wstring& path, FileType expected)
{
    FileType actual = DetectFileType(path);
    if (actual == expected) return true;

    const wchar_t* expectedName = L"未知";
    const wchar_t* actualName = L"未知";
    switch (expected) {
    case FileType::Script:    expectedName = L"脚本";   break;
    case FileType::TimeRange: expectedName = L"时间段"; break;
    case FileType::Config:    expectedName = L"配置";   break;
    default: break;
    }
    switch (actual) {
    case FileType::Script:    actualName = L"脚本";       break;
    case FileType::TimeRange: actualName = L"时间段";     break;
    case FileType::Config:    actualName = L"配置";       break;
    default:                  actualName = L"未知或无效"; break;
    }

    std::wstring msg = L"文件类型不匹配！\n\n";
    msg += L"您正在导入：" + std::wstring(expectedName) + L"文件\n";
    msg += L"但该文件实际是：" + std::wstring(actualName) + L"文件\n\n";
    msg += L"请检查是否选错了文件。";
    MessageBoxW(hDlg, msg.c_str(), L"文件类型错误", MB_ICONWARNING);
    return false;
}