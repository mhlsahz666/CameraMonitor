#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <string>
#include <vector>

// ======================== 逻辑运算符 ========================
enum class LogicalOp {
    Equal,        // 等于
    NotEqual,     // 不等于
    Greater,      // 大于
    Less,         // 小于
    Contains,     // 包含
};

// ======================== 动作类型 ========================
enum class ScriptActionType {
    // ===== 普通动作 =====
    RunCmd,
    PlaySoundAction,
    SimulateKey,
    CreateWindow,
    CustomNotify,
    Wait,
    RandomNumber,
    ExitProgram,

    // ===== 块 =====
    BlockEventCameraStart,      // 当摄像头被占用
    BlockEventCameraStop,       // 当摄像头停止占用
    BlockIfCameraOccupied,      // 如果 xx 摄像头被占用
    BlockIfProcessOccupied,     // 如果 xx 程序占用
    BlockIfRandom,              // 如果随机数 > 50
    BlockRepeat,                // 重复执行
    BlockIf,                    // 如果（通用条件）
};

// ======================== 动作结构 ========================
struct ScriptAction {
    ScriptActionType type;
    std::wstring param1;
    std::wstring param2;
    int delay = 0;
    int repeatCount = 1;
    int soundWait = 0;
    int randomMin = 0;
    int randomMax = 100;
    LogicalOp logicalOp = LogicalOp::Equal;  // 逻辑运算符（用于 BlockIf）
    bool enabled = true;
    std::vector<ScriptAction> children;
};

enum class ScriptEvent {
    CameraStart,
    CameraStop
};

// ======================== 判断是否为块 ========================
inline bool IsBlockAction(ScriptActionType t) {
    switch (t) {
    case ScriptActionType::BlockEventCameraStart:
    case ScriptActionType::BlockEventCameraStop:
    case ScriptActionType::BlockIfCameraOccupied:
    case ScriptActionType::BlockIfProcessOccupied:
    case ScriptActionType::BlockIfRandom:
    case ScriptActionType::BlockRepeat:
    case ScriptActionType::BlockIf:
        return true;
    }
    return false;
}

// ======================== 脚本引擎 ========================
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
    void ExecuteRandomNumber(const ScriptAction& action);
};