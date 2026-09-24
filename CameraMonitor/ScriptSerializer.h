#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ScriptEngine.h"

class ScriptSerializer {
public:
    // 导出脚本
    static bool Export(const std::wstring& path, const std::vector<ScriptAction>& actions);

    // 导入脚本
    static bool Import(const std::wstring& path, std::vector<ScriptAction>& actions);

private:
    static void WriteAction(FILE* f, const ScriptAction& a, const std::wstring& prefix);
    static bool ReadAction(FILE* f, const std::wstring& prefix, ScriptAction& a);
};