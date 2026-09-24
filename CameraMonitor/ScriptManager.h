#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <string>
#include <vector>
#include "ScriptEngine.h"

// 单个脚本（包含名称、动作列表、启用状态）
struct ScriptInfo {
    std::wstring name;
    std::vector<ScriptAction> actions;
    bool enabled = true;
};

class ScriptManager {
public:
    ScriptManager();

    // 加载/保存所有脚本
    void LoadAll();
    void SaveAll() const;

    // 脚本列表操作
    int  GetCount() const { return (int)m_startScripts.size(); }
    ScriptInfo* GetScript(int index);
    const ScriptInfo* GetScript(int index) const;

    int  AddScript(const std::wstring& name);
    bool RemoveScript(int index);
    bool RenameScript(int index, const std::wstring& newName);

    // 事件脚本（开始/停止时触发的动作列表）
    std::vector<ScriptAction> GetEventActions(ScriptEvent evt) const;
    void SetEventActions(ScriptEvent evt, const std::vector<ScriptAction>& actions);
    bool IsEventEnabled(ScriptEvent evt) const;
    void SetEventEnabled(ScriptEvent evt, bool enabled);

    // 计算脚本大小（字节）
    static size_t GetScriptSize(const ScriptInfo& info);

private:
    std::vector<ScriptInfo> m_startScripts;
    std::vector<ScriptInfo> m_stopScripts;

    void LoadFromRegistry(const wchar_t* prefix, std::vector<ScriptInfo>& out);
    void SaveToRegistry(const wchar_t* prefix, const std::vector<ScriptInfo>& scripts) const;
};