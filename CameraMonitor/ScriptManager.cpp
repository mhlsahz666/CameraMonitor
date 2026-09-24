#include "ScriptManager.h"
#include "AutoStart.h"

ScriptManager::ScriptManager() {}

// ======================== 加载/保存 ========================
void ScriptManager::LoadAll()
{
    m_startScripts.clear();
    m_stopScripts.clear();
    LoadFromRegistry(L"StartScript_", m_startScripts);
    LoadFromRegistry(L"StopScript_", m_stopScripts);
}

void ScriptManager::SaveAll() const
{
    SaveToRegistry(L"StartScript_", m_startScripts);
    SaveToRegistry(L"StopScript_", m_stopScripts);
}

// ======================== 递归加载动作 ========================
// 路径示例：ScriptStart_0_Action3_
static void LoadActionRecursive(const std::wstring& prefix,
    ScriptAction& a)
{
    a.type = (ScriptActionType)ConfigStore::LoadInt(prefix + L"Type", 0);
    a.param1 = ConfigStore::LoadString(prefix + L"Param1", L"");
    a.param2 = ConfigStore::LoadString(prefix + L"Param2", L"");
    a.repeatCount = ConfigStore::LoadInt(prefix + L"RepeatCount", 1);
    a.delay = ConfigStore::LoadInt(prefix + L"Delay", 0);
    a.soundWait = ConfigStore::LoadInt(prefix + L"SoundWait", 0);
    a.randomMin = ConfigStore::LoadInt(prefix + L"RandomMin", 0);
    a.randomMax = ConfigStore::LoadInt(prefix + L"RandomMax", 100);
    a.logicalOp = (LogicalOp)ConfigStore::LoadInt(prefix + L"LogicalOp", 0);
    a.enabled = ConfigStore::LoadInt(prefix + L"Enabled", 1) != 0;

    int childCount = ConfigStore::LoadInt(prefix + L"ChildCount", 0);
    for (int i = 0; i < childCount; i++) {
        wchar_t idx[32];
        swprintf_s(idx, L"Child%d_", i);
        std::wstring childPrefix = prefix + idx;

        ScriptAction child;
        LoadActionRecursive(childPrefix, child);
        a.children.push_back(child);
    }
}

// ======================== 递归保存动作 ========================
static void SaveActionRecursive(const std::wstring& prefix,
    const ScriptAction& a)
{
    ConfigStore::SaveInt(prefix + L"Type", (int)a.type);
    ConfigStore::SaveString(prefix + L"Param1", a.param1);
    ConfigStore::SaveString(prefix + L"Param2", a.param2);
    ConfigStore::SaveInt(prefix + L"RepeatCount", a.repeatCount);
    ConfigStore::SaveInt(prefix + L"Delay", a.delay);
    ConfigStore::SaveInt(prefix + L"SoundWait", a.soundWait);
    ConfigStore::SaveInt(prefix + L"RandomMin", a.randomMin);
    ConfigStore::SaveInt(prefix + L"RandomMax", a.randomMax);
    ConfigStore::SaveInt(prefix + L"LogicalOp", (int)a.logicalOp);
    ConfigStore::SaveInt(prefix + L"Enabled", a.enabled ? 1 : 0);

    ConfigStore::SaveInt(prefix + L"ChildCount", (int)a.children.size());
    for (size_t i = 0; i < a.children.size(); i++) {
        wchar_t idx[32];
        swprintf_s(idx, L"Child%zu_", i);
        std::wstring childPrefix = prefix + idx;
        SaveActionRecursive(childPrefix, a.children[i]);
    }
}

// ======================== 加载脚本列表 ========================
void ScriptManager::LoadFromRegistry(const wchar_t* prefix, std::vector<ScriptInfo>& out)
{
    int count = ConfigStore::LoadInt(std::wstring(prefix) + L"Count", 0);
    for (int i = 0; i < count; i++) {
        wchar_t idx[32];
        swprintf_s(idx, L"%d_", i);
        std::wstring p = std::wstring(prefix) + idx;

        ScriptInfo info;
        info.name = ConfigStore::LoadString(p + L"Name", L"未命名脚本");
        info.enabled = ConfigStore::LoadInt(p + L"Enabled", 1) != 0;

        int actionCount = ConfigStore::LoadInt(p + L"ActionCount", 0);
        for (int j = 0; j < actionCount; j++) {
            wchar_t aidx[32];
            swprintf_s(aidx, L"Action%d_", j);
            std::wstring ap = p + aidx;

            ScriptAction a;
            LoadActionRecursive(ap, a);
            info.actions.push_back(a);
        }
        out.push_back(info);
    }
}

// ======================== 保存脚本列表 ========================
void ScriptManager::SaveToRegistry(const wchar_t* prefix, const std::vector<ScriptInfo>& scripts) const
{
    ConfigStore::SaveInt(std::wstring(prefix) + L"Count", (int)scripts.size());
    for (size_t i = 0; i < scripts.size(); i++) {
        wchar_t idx[32];
        swprintf_s(idx, L"%zu_", i);
        std::wstring p = std::wstring(prefix) + idx;

        ConfigStore::SaveString(p + L"Name", scripts[i].name);
        ConfigStore::SaveInt(p + L"Enabled", scripts[i].enabled ? 1 : 0);
        ConfigStore::SaveInt(p + L"ActionCount", (int)scripts[i].actions.size());

        for (size_t j = 0; j < scripts[i].actions.size(); j++) {
            wchar_t aidx[32];
            swprintf_s(aidx, L"Action%zu_", j);
            std::wstring ap = p + aidx;

            SaveActionRecursive(ap, scripts[i].actions[j]);
        }
    }
}

// ======================== 脚本操作 ========================
ScriptInfo* ScriptManager::GetScript(int index)
{
    if (index < 0 || index >= (int)m_startScripts.size()) return nullptr;
    return &m_startScripts[index];
}

const ScriptInfo* ScriptManager::GetScript(int index) const
{
    if (index < 0 || index >= (int)m_startScripts.size()) return nullptr;
    return &m_startScripts[index];
}

int ScriptManager::AddScript(const std::wstring& name)
{
    ScriptInfo info;
    info.name = name;
    info.enabled = true;
    m_startScripts.push_back(info);
    return (int)m_startScripts.size() - 1;
}

bool ScriptManager::RemoveScript(int index)
{
    if (index < 0 || index >= (int)m_startScripts.size()) return false;
    m_startScripts.erase(m_startScripts.begin() + index);
    return true;
}

bool ScriptManager::RenameScript(int index, const std::wstring& newName)
{
    if (index < 0 || index >= (int)m_startScripts.size()) return false;
    m_startScripts[index].name = newName;
    return true;
}

// ======================== 事件脚本 ========================
std::vector<ScriptAction> ScriptManager::GetEventActions(ScriptEvent evt) const
{
    std::vector<ScriptAction> result;
    const auto& list = (evt == ScriptEvent::CameraStart) ? m_startScripts : m_stopScripts;
    for (const auto& s : list) {
        if (s.enabled) {
            for (const auto& a : s.actions) result.push_back(a);
        }
    }
    return result;
}

void ScriptManager::SetEventActions(ScriptEvent evt, const std::vector<ScriptAction>& actions)
{
    auto& list = (evt == ScriptEvent::CameraStart) ? m_startScripts : m_stopScripts;
    if (list.empty()) {
        ScriptInfo info;
        info.name = L"默认脚本";
        info.actions = actions;
        list.push_back(info);
    }
    else {
        list[0].actions = actions;
    }
}

bool ScriptManager::IsEventEnabled(ScriptEvent evt) const
{
    const auto& list = (evt == ScriptEvent::CameraStart) ? m_startScripts : m_stopScripts;
    for (const auto& s : list) if (s.enabled) return true;
    return false;
}

void ScriptManager::SetEventEnabled(ScriptEvent evt, bool enabled)
{
    auto& list = (evt == ScriptEvent::CameraStart) ? m_startScripts : m_stopScripts;
    for (auto& s : list) s.enabled = enabled;
}

// ======================== 计算脚本大小（递归）========================
static size_t CountActionSize(const ScriptAction& a)
{
    size_t size = sizeof(ScriptAction);
    size += a.param1.size() * sizeof(wchar_t);
    size += a.param2.size() * sizeof(wchar_t);
    for (const auto& c : a.children) {
        size += CountActionSize(c);
    }
    return size;
}

size_t ScriptManager::GetScriptSize(const ScriptInfo& info)
{
    size_t size = info.name.size() * sizeof(wchar_t);
    for (const auto& a : info.actions) {
        size += CountActionSize(a);
    }
    return size;
}