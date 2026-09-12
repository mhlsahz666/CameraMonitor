#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <string>
#include <vector>

enum class ScriptActionType {
    RunCmd,
    PlaySoundAction,
    SimulateKey,
    CreateWindow,
    CustomNotify,
    Wait,
    Repeat,
    IfCameraInUse,
    ExitProgram
};

struct ScriptAction {
    ScriptActionType type;
    std::wstring param1;
    std::wstring param2;
    int delay = 0;
    int repeatCount = 1;
    int soundWait = 0;   // ← 新增：0=不等待，1=等待播放完毕
    std::vector<ScriptAction> children;
};

enum class ScriptEvent {
    CameraStart,
    CameraStop
};

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    void SetScript(ScriptEvent evt, const std::vector<ScriptAction>& actions);
    std::vector<ScriptAction> GetScript(ScriptEvent evt) const;
    void TriggerEvent(ScriptEvent evt);
    void ExecuteAction(const ScriptAction& action);

private:
    std::vector<ScriptAction> m_startScript;
    std::vector<ScriptAction> m_stopScript;
    CRITICAL_SECTION m_cs;

    void ExecuteActions(const std::vector<ScriptAction>& actions);
    void ExecuteRunCmd(const std::wstring& cmd);
    void ExecutePlaySound(const std::wstring& path, bool wait);
    void ExecuteSimulateKey(const std::wstring& key);
    void ExecuteCreateWindow(const std::wstring& title, const std::wstring& text);
    void ExecuteCustomNotify(const std::wstring& title, const std::wstring& text);
};