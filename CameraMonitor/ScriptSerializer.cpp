#include "ScriptSerializer.h"
#include "FileHeader.h"
#include <cstdio>

// ======================== 导出：递归写动作 ========================
void ScriptSerializer::WriteAction(FILE* f, const ScriptAction& a, const std::wstring& prefix)
{
    fwprintf(f, L"[%s]\n", prefix.c_str());
    fwprintf(f, L"Type=%d\n", (int)a.type);
    fwprintf(f, L"Param1=%s\n", a.param1.c_str());
    fwprintf(f, L"Param2=%s\n", a.param2.c_str());
    fwprintf(f, L"Repeat=%d\n", a.repeatCount);
    fwprintf(f, L"Delay=%d\n", a.delay);
    fwprintf(f, L"SoundWait=%d\n", a.soundWait);
    fwprintf(f, L"RMin=%d\n", a.randomMin);
    fwprintf(f, L"RMax=%d\n", a.randomMax);
    fwprintf(f, L"Op=%d\n", (int)a.logicalOp);
    fwprintf(f, L"Enabled=%d\n", a.enabled ? 1 : 0);
    fwprintf(f, L"ChildCount=%d\n", (int)a.children.size());

    for (size_t i = 0; i < a.children.size(); i++) {
        wchar_t idx[64];
        swprintf_s(idx, L"%s.%zu", prefix.c_str(), i);
        WriteAction(f, a.children[i], idx);
    }
}

bool ScriptSerializer::Export(const std::wstring& path, const std::vector<ScriptAction>& actions)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;

    fwprintf(f, L"%s\n", HEADER_SCRIPT);
    fwprintf(f, L"TopCount=%zu\n", actions.size());

    for (size_t i = 0; i < actions.size(); i++) {
        wchar_t idx[32];
        swprintf_s(idx, L"%zu", i);
        WriteAction(f, actions[i], idx);
    }

    fclose(f);
    return true;
}

// ======================== 导入：按段读取 ========================
bool ScriptSerializer::ReadAction(FILE* f, const std::wstring& prefix, ScriptAction& a)
{
    // 先读取整个段
    struct KV { std::wstring key, val; };
    std::vector<KV> kvs;
    int childCount = 0;

    wchar_t line[2048];
    while (fgetws(line, 2048, f)) {
        std::wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r'))
            s.pop_back();
        if (s.empty()) continue;

        if (s[0] == L'[') {
            // 段头
            std::wstring secName = s.substr(1, s.size() - 2);
            if (secName == prefix) {
                // 进入本段
                continue;
            }
            else {
                // 遇到别的段，回退一行
                _fseeki64(f, -(long long)(s.size() + 2) * sizeof(wchar_t), SEEK_CUR);
                break;
            }
        }

        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = s.substr(0, eq);
        std::wstring val = s.substr(eq + 1);
        kvs.push_back({ key, val });

        if (key == L"ChildCount") {
            childCount = _wtoi(val.c_str());
            break;
        }
    }

    // 应用键值
    for (const auto& kv : kvs) {
        if (kv.key == L"Type")          a.type = (ScriptActionType)_wtoi(kv.val.c_str());
        else if (kv.key == L"Param1")   a.param1 = kv.val;
        else if (kv.key == L"Param2")   a.param2 = kv.val;
        else if (kv.key == L"Repeat")   a.repeatCount = _wtoi(kv.val.c_str());
        else if (kv.key == L"Delay")    a.delay = _wtoi(kv.val.c_str());
        else if (kv.key == L"SoundWait") a.soundWait = _wtoi(kv.val.c_str());
        else if (kv.key == L"RMin")     a.randomMin = _wtoi(kv.val.c_str());
        else if (kv.key == L"RMax")     a.randomMax = _wtoi(kv.val.c_str());
        else if (kv.key == L"Op")       a.logicalOp = (LogicalOp)_wtoi(kv.val.c_str());
        else if (kv.key == L"Enabled")  a.enabled = _wtoi(kv.val.c_str()) != 0;
    }

    // 递归读子动作
    for (int i = 0; i < childCount; i++) {
        wchar_t idx[64];
        swprintf_s(idx, L"%s.%d", prefix.c_str(), i);
        ScriptAction child;
        ReadAction(f, idx, child);
        a.children.push_back(child);
    }
    return true;
}

bool ScriptSerializer::Import(const std::wstring& path, std::vector<ScriptAction>& actions)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) return false;

    wchar_t line[2048];
    // 读文件头
    fgetws(line, 2048, f);

    // 读 TopCount
    int topCount = 0;
    while (fgetws(line, 2048, f)) {
        std::wstring s = line;
        if (!s.empty() && s.back() == L'\n') s.pop_back();
        if (s.find(L"TopCount=") == 0) {
            topCount = _wtoi(s.substr(9).c_str());
            break;
        }
    }

    actions.clear();
    for (int i = 0; i < topCount; i++) {
        wchar_t idx[32];
        swprintf_s(idx, L"%d", i);
        ScriptAction a;
        ReadAction(f, idx, a);
        actions.push_back(a);
    }

    fclose(f);
    return true;
}