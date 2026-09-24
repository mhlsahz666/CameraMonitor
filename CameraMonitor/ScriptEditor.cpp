#include "ScriptEditor.h"
#include <sstream>

std::vector<ScriptAction>* ScriptEditor::s_actions = nullptr;
std::wstring ScriptEditor::s_scriptName;
bool ScriptEditor::s_isNew = false;
std::vector<std::vector<int>> ScriptEditor::s_linePaths;

// ======================== 动作名称 ========================
static const wchar_t* ActionDisplayName(ScriptActionType t)
{
    switch (t) {
    case ScriptActionType::RunCmd:                  return L"执行 CMD 命令";
    case ScriptActionType::PlaySoundAction:         return L"播放声音";
    case ScriptActionType::SimulateKey:             return L"模拟按键";
    case ScriptActionType::CreateWindow:            return L"弹出窗口";
    case ScriptActionType::CustomNotify:            return L"托盘通知";
    case ScriptActionType::Wait:                    return L"等待";
    case ScriptActionType::RandomNumber:            return L"随机数";
    case ScriptActionType::ExitProgram:             return L"退出程序";
    case ScriptActionType::BlockEventCameraStart:   return L"当摄像头被占用";
    case ScriptActionType::BlockEventCameraStop:    return L"当摄像头停止占用";
    case ScriptActionType::BlockIfCameraOccupied:   return L"如果摄像头被占用";
    case ScriptActionType::BlockIfProcessOccupied:  return L"如果程序占用";
    case ScriptActionType::BlockIfRandom:           return L"如果随机数 > 50";
    case ScriptActionType::BlockRepeat:             return L"重复执行";
    case ScriptActionType::BlockIf:                 return L"如果";
    }
    return L"未知";
}

// ======================== 动作提示 ========================
static const wchar_t* ActionHint(ScriptActionType t)
{
    switch (t) {
    case ScriptActionType::RunCmd:                  return L"参数 1：命令，例如 notepad.exe";
    case ScriptActionType::PlaySoundAction:         return L"参数 1：声音文件路径";
    case ScriptActionType::SimulateKey:             return L"参数 1：按键组合，可点“录制”";
    case ScriptActionType::CreateWindow:            return L"参数 1：窗口标题\n参数 2：窗口内容";
    case ScriptActionType::CustomNotify:            return L"参数 1：通知标题\n参数 2：通知内容";
    case ScriptActionType::Wait:                    return L"参数 1：等待毫秒数";
    case ScriptActionType::RandomNumber:            return L"生成随机数并通知";
    case ScriptActionType::ExitProgram:             return L"无需参数";
    case ScriptActionType::BlockEventCameraStart:   return L"当摄像头被占用时执行 { } 内的动作";
    case ScriptActionType::BlockEventCameraStop:    return L"当摄像头停止占用时执行 { } 内的动作";
    case ScriptActionType::BlockIfCameraOccupied:   return L"参数 1：摄像头友好名（留空表示任意摄像头）\n"
        L"例如：Logitech、Integrated Camera";
    case ScriptActionType::BlockIfProcessOccupied:  return L"参数 1：进程名（如 WindowsCamera.exe），包含匹配";
    case ScriptActionType::BlockIfRandom:           return L"如果随机数大于 50，执行 { } 内的动作";
    case ScriptActionType::BlockRepeat:             return L"参数 1：重复次数";
    case ScriptActionType::BlockIf:                 return L"参数 1：变量名（进程名 / 摄像头名 / 随机数）\n"
        L"参数 2：比较值\n"
        L"逻辑运算符：等于 / 不等于 / 大于 / 小于 / 包含";
    }
    return L"";
}

// ======================== 动作编辑对话框 ========================
struct ActionEditContext {
    ScriptAction* action = nullptr;
    bool ok = false;
};

static void ShowCtrl(HWND hDlg, int id, bool show) {
    HWND h = GetDlgItem(hDlg, id);
    if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
}

static const wchar_t* g_singleKeys[] = {
    L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J",
    L"K", L"L", L"M", L"N", L"O", L"P", L"Q", L"R", L"S", L"T",
    L"U", L"V", L"W", L"X", L"Y", L"Z",
    L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9",
    L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8",
    L"F9", L"F10", L"F11", L"F12",
    L"ENTER", L"ESC", L"TAB", L"SPACE", L"BACK", L"DELETE",
    L"UP", L"DOWN", L"LEFT", L"RIGHT",
    L"INSERT", L"HOME", L"END", L"PGUP", L"PGDN",
    L"WIN", L"CAPSLOCK", L"NUMLOCK", L"SCROLLLOCK", L"PRINTSCREEN",
    L"NUM0", L"NUM1", L"NUM2", L"NUM3", L"NUM4",
    L"NUM5", L"NUM6", L"NUM7", L"NUM8", L"NUM9"
};

static INT_PTR CALLBACK ActionEditProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static ActionEditContext* ctx = nullptr;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        ctx = (ActionEditContext*)lParam;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);

        HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        EnumChildWindows(hDlg, [](HWND hChild, LPARAM lp) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
            }, (LPARAM)hFont);

        HWND hSingleKey = GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY);
        for (int i = 0; i < _countof(g_singleKeys); i++)
            SendMessageW(hSingleKey, CB_ADDSTRING, 0, (LPARAM)g_singleKeys[i]);

        HWND hSoundWait = GetDlgItem(hDlg, IDC_COMBO_SOUNDWAIT);
        SendMessageW(hSoundWait, CB_ADDSTRING, 0, (LPARAM)L"不等待（后台播放）");
        SendMessageW(hSoundWait, CB_ADDSTRING, 0, (LPARAM)L"等待播放完毕");
        SendMessageW(hSoundWait, CB_SETCURSEL, 0, 0);

        HWND hLogicalOp = GetDlgItem(hDlg, IDC_COMBO_LOGICAL_OP);
        SendMessageW(hLogicalOp, CB_ADDSTRING, 0, (LPARAM)L"等于");
        SendMessageW(hLogicalOp, CB_ADDSTRING, 0, (LPARAM)L"不等于");
        SendMessageW(hLogicalOp, CB_ADDSTRING, 0, (LPARAM)L"大于");
        SendMessageW(hLogicalOp, CB_ADDSTRING, 0, (LPARAM)L"小于");
        SendMessageW(hLogicalOp, CB_ADDSTRING, 0, (LPARAM)L"包含");
        SendMessageW(hLogicalOp, CB_SETCURSEL, 0, 0);

        CheckRadioButton(hDlg, IDC_RADIO_ACTION, IDC_RADIO_BLOCK, IDC_RADIO_ACTION);
        SetDlgItemInt(hDlg, IDC_EDIT_REPEAT, 1, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_RANDOM_MIN, 0, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_RANDOM_MAX, 100, FALSE);

        if (ctx && ctx->action) {
            bool isBlock = IsBlockAction(ctx->action->type);
            CheckRadioButton(hDlg, IDC_RADIO_ACTION, IDC_RADIO_BLOCK,
                isBlock ? IDC_RADIO_BLOCK : IDC_RADIO_ACTION);

            SetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, ctx->action->param1.c_str());
            SetDlgItemTextW(hDlg, IDC_EDIT_PARAM2, ctx->action->param2.c_str());
            SetDlgItemInt(hDlg, IDC_EDIT_REPEAT, ctx->action->repeatCount, FALSE);
            SendMessageW(hSoundWait, CB_SETCURSEL, ctx->action->soundWait ? 1 : 0, 0);
            SendMessageW(hLogicalOp, CB_SETCURSEL, (int)ctx->action->logicalOp, 0);
            SetDlgItemInt(hDlg, IDC_EDIT_RANDOM_MIN, ctx->action->randomMin, FALSE);
            SetDlgItemInt(hDlg, IDC_EDIT_RANDOM_MAX, ctx->action->randomMax, FALSE);
        }

        // 填充下拉框并刷新显示
        SendMessageW(hDlg, WM_COMMAND,
            MAKEWPARAM(IDC_RADIO_ACTION, BN_CLICKED),
            (LPARAM)GetDlgItem(hDlg, IDC_RADIO_ACTION));

        if (ctx && ctx->action) {
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE);
            int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                wchar_t buf[128];
                SendMessageW(hCombo, CB_GETLBTEXT, i, (LPARAM)buf);
                if (wcscmp(buf, ActionDisplayName(ctx->action->type)) == 0) {
                    SendMessageW(hCombo, CB_SETCURSEL, i, 0);
                    break;
                }
            }
            SendMessageW(hDlg, WM_COMMAND,
                MAKEWPARAM(IDC_COMBO_ACTION_TYPE, CBN_SELCHANGE),
                (LPARAM)hCombo);
        }
    }
    return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_RADIO_ACTION || LOWORD(wParam) == IDC_RADIO_BLOCK) {
            bool isBlock = (IsDlgButtonChecked(hDlg, IDC_RADIO_BLOCK) == BST_CHECKED);
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE);
            SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

            if (isBlock) {
                const wchar_t* blocks[] = {
                    L"当摄像头被占用", L"当摄像头停止占用",
                    L"如果摄像头被占用", L"如果程序占用",
                    L"如果随机数 > 50", L"重复执行", L"如果"
                };
                for (int i = 0; i < 7; i++)
                    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)blocks[i]);
            }
            else {
                const wchar_t* actions[] = {
                    L"执行 CMD 命令", L"播放声音", L"模拟按键",
                    L"弹出窗口", L"托盘通知", L"等待",
                    L"随机数", L"退出程序"
                };
                for (int i = 0; i < 8; i++)
                    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)actions[i]);
            }
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

            SendMessageW(hDlg, WM_COMMAND,
                MAKEWPARAM(IDC_COMBO_ACTION_TYPE, CBN_SELCHANGE),
                (LPARAM)hCombo);
        }
        else if (LOWORD(wParam) == IDC_COMBO_ACTION_TYPE && HIWORD(wParam) == CBN_SELCHANGE) {
            bool isBlock = (IsDlgButtonChecked(hDlg, IDC_RADIO_BLOCK) == BST_CHECKED);
            int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE), CB_GETCURSEL, 0, 0);

            ScriptActionType t;
            if (isBlock) {
                switch (sel) {
                case 0: t = ScriptActionType::BlockEventCameraStart; break;
                case 1: t = ScriptActionType::BlockEventCameraStop; break;
                case 2: t = ScriptActionType::BlockIfCameraOccupied; break;
                case 3: t = ScriptActionType::BlockIfProcessOccupied; break;
                case 4: t = ScriptActionType::BlockIfRandom; break;
                case 5: t = ScriptActionType::BlockRepeat; break;
                case 6: t = ScriptActionType::BlockIf; break;
                default: t = ScriptActionType::BlockIf; break;
                }
            }
            else {
                switch (sel) {
                case 0: t = ScriptActionType::RunCmd; break;
                case 1: t = ScriptActionType::PlaySoundAction; break;
                case 2: t = ScriptActionType::SimulateKey; break;
                case 3: t = ScriptActionType::CreateWindow; break;
                case 4: t = ScriptActionType::CustomNotify; break;
                case 5: t = ScriptActionType::Wait; break;
                case 6: t = ScriptActionType::RandomNumber; break;
                case 7: t = ScriptActionType::ExitProgram; break;
                default: t = ScriptActionType::RunCmd; break;
                }
            }

            SetDlgItemTextW(hDlg, IDC_STATIC_HINT, ActionHint(t));

            bool needParam1 = true;
            bool needParam2 = false;
            bool needRepeat = false;
            bool needSound = false;
            bool needSingleKey = false;
            bool needRandom = false;
            bool needLogicalOp = false;

            switch (t) {
            case ScriptActionType::ExitProgram:
            case ScriptActionType::BlockEventCameraStart:
            case ScriptActionType::BlockEventCameraStop:
            case ScriptActionType::BlockIfRandom:
                needParam1 = false;
                break;

            case ScriptActionType::BlockIfCameraOccupied:
                needParam1 = true;
                SetDlgItemTextW(hDlg, IDC_STATIC_PARAM1, L"摄像头名：");
                break;

            case ScriptActionType::BlockIfProcessOccupied:
                needParam1 = true;
                SetDlgItemTextW(hDlg, IDC_STATIC_PARAM1, L"进程名：");
                break;

            case ScriptActionType::BlockIf:
                needParam1 = true;
                needParam2 = true;
                needLogicalOp = true;
                SetDlgItemTextW(hDlg, IDC_STATIC_PARAM1, L"变量名：");
                break;

            case ScriptActionType::BlockRepeat:
                needParam1 = false;
                needRepeat = true;
                break;

            case ScriptActionType::CreateWindow:
            case ScriptActionType::CustomNotify:
                needParam1 = true;
                needParam2 = true;
                break;

            case ScriptActionType::SimulateKey:
                needParam1 = true;
                needSingleKey = true;
                break;

            case ScriptActionType::PlaySoundAction:
                needParam1 = true;
                needSound = true;
                break;

            case ScriptActionType::RandomNumber:
                needRandom = true;
                needParam1 = false;
                break;

            default:
                needParam1 = true;
                break;
            }

            if (t != ScriptActionType::BlockIfProcessOccupied &&
                t != ScriptActionType::BlockIf &&
                t != ScriptActionType::BlockIfCameraOccupied) {
                SetDlgItemTextW(hDlg, IDC_STATIC_PARAM1, L"参数 1：");
            }

            ShowCtrl(hDlg, IDC_STATIC_PARAM1, needParam1);
            ShowCtrl(hDlg, IDC_EDIT_PARAM1, needParam1);
            ShowCtrl(hDlg, IDC_STATIC_PARAM2, needParam2);
            ShowCtrl(hDlg, IDC_EDIT_PARAM2, needParam2);
            ShowCtrl(hDlg, IDC_STATIC_REPEAT, needRepeat);
            ShowCtrl(hDlg, IDC_EDIT_REPEAT, needRepeat);
            ShowCtrl(hDlg, IDC_STATIC_SOUNDWAIT, needSound);
            ShowCtrl(hDlg, IDC_COMBO_SOUNDWAIT, needSound);
            ShowCtrl(hDlg, IDC_BTN_RECORD_KEY, needSingleKey);
            ShowCtrl(hDlg, IDC_STATIC_SINGLEKEY, needSingleKey);
            ShowCtrl(hDlg, IDC_COMBO_SINGLE_KEY, needSingleKey);
            ShowCtrl(hDlg, IDC_STATIC_RANDOM, needRandom);
            ShowCtrl(hDlg, IDC_EDIT_RANDOM_MIN, needRandom);
            ShowCtrl(hDlg, IDC_EDIT_RANDOM_MAX, needRandom);
            ShowCtrl(hDlg, IDC_STATIC_LOGICAL_OP, needLogicalOp);
            ShowCtrl(hDlg, IDC_COMBO_LOGICAL_OP, needLogicalOp);
        }
        else if (LOWORD(wParam) == IDC_COMBO_SINGLE_KEY && HIWORD(wParam) == CBN_SELCHANGE) {
            int idx = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY), CB_GETCURSEL, 0, 0);
            if (idx != CB_ERR) {
                wchar_t buf[64];
                SendMessageW(GetDlgItem(hDlg, IDC_COMBO_SINGLE_KEY), CB_GETLBTEXT, idx, (LPARAM)buf);
                SetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, buf);
            }
        }
        else if (LOWORD(wParam) == IDC_BTN_ACTION_OK) {
            if (ctx && ctx->action) {
                bool isBlock = (IsDlgButtonChecked(hDlg, IDC_RADIO_BLOCK) == BST_CHECKED);
                int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_ACTION_TYPE), CB_GETCURSEL, 0, 0);

                ScriptActionType t;
                if (isBlock) {
                    switch (sel) {
                    case 0: t = ScriptActionType::BlockEventCameraStart; break;
                    case 1: t = ScriptActionType::BlockEventCameraStop; break;
                    case 2: t = ScriptActionType::BlockIfCameraOccupied; break;
                    case 3: t = ScriptActionType::BlockIfProcessOccupied; break;
                    case 4: t = ScriptActionType::BlockIfRandom; break;
                    case 5: t = ScriptActionType::BlockRepeat; break;
                    case 6: t = ScriptActionType::BlockIf; break;
                    default: t = ScriptActionType::BlockIf; break;
                    }
                }
                else {
                    switch (sel) {
                    case 0: t = ScriptActionType::RunCmd; break;
                    case 1: t = ScriptActionType::PlaySoundAction; break;
                    case 2: t = ScriptActionType::SimulateKey; break;
                    case 3: t = ScriptActionType::CreateWindow; break;
                    case 4: t = ScriptActionType::CustomNotify; break;
                    case 5: t = ScriptActionType::Wait; break;
                    case 6: t = ScriptActionType::RandomNumber; break;
                    case 7: t = ScriptActionType::ExitProgram; break;
                    default: t = ScriptActionType::RunCmd; break;
                    }
                }
                ctx->action->type = t;

                wchar_t buf[512];
                GetDlgItemTextW(hDlg, IDC_EDIT_PARAM1, buf, 512);
                ctx->action->param1 = buf;
                GetDlgItemTextW(hDlg, IDC_EDIT_PARAM2, buf, 512);
                ctx->action->param2 = buf;
                ctx->action->repeatCount = GetDlgItemInt(hDlg, IDC_EDIT_REPEAT, NULL, FALSE);
                ctx->action->soundWait = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_SOUNDWAIT), CB_GETCURSEL, 0, 0);
                ctx->action->logicalOp = (LogicalOp)(int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_LOGICAL_OP), CB_GETCURSEL, 0, 0);
                ctx->action->randomMin = GetDlgItemInt(hDlg, IDC_EDIT_RANDOM_MIN, NULL, FALSE);
                ctx->action->randomMax = GetDlgItemInt(hDlg, IDC_EDIT_RANDOM_MAX, NULL, FALSE);
                ctx->ok = true;
            }
            EndDialog(hDlg, IDOK);
        }
        else if (LOWORD(wParam) == IDC_BTN_ACTION_CANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static bool EditSingleAction(HWND parent, ScriptAction& action)
{
    ActionEditContext ctx;
    ctx.action = &action;
    ctx.ok = false;
    INT_PTR ret = DialogBoxParamW(GetModuleHandle(NULL),
        MAKEINTRESOURCEW(IDD_ACTION_EDITOR), parent, ActionEditProc, (LPARAM)&ctx);
    return ret == IDOK && ctx.ok;
}

// ======================== 大括号渲染 ========================
void ScriptEditor::AddIndent(std::wstring& text, int level)
{
    for (int i = 0; i < level; i++)
        text += L"    ";
}

static void RenderActionWithPaths(const std::vector<ScriptAction>& actions,
    std::vector<int> parentPath,
    std::vector<std::vector<int>>& outPaths,
    std::vector<std::wstring>& outLines,
    int level)
{
    for (size_t i = 0; i < actions.size(); i++) {
        std::vector<int> path = parentPath;
        path.push_back((int)i);

        const ScriptAction& a = actions[i];
        std::wstring indent;
        for (int k = 0; k < level; k++) indent += L"    ";

        if (IsBlockAction(a.type)) {
            std::wstring head = indent + ActionDisplayName(a.type);
            if (!a.param1.empty()) head += L" (" + a.param1 + L")";
            head += L" {";
            outLines.push_back(head);
            outPaths.push_back(path);

            RenderActionWithPaths(a.children, path, outPaths, outLines, level + 1);

            outLines.push_back(indent + L"}");
            outPaths.push_back(path);
        }
        else {
            std::wstring line = indent + ActionDisplayName(a.type);
            if (!a.param1.empty()) line += L" (" + a.param1 + L")";
            outLines.push_back(line);
            outPaths.push_back(path);
        }
    }
}

// ======================== 脚本编辑器主流程 ========================
bool ScriptEditor::Show(HINSTANCE hInst, HWND parent,
    std::vector<ScriptAction>& actions,
    const std::wstring& scriptName, bool isNew)
{
    s_actions = &actions;
    s_scriptName = scriptName;
    s_isNew = isNew;

    INT_PTR ret = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SCRIPT_EDITOR),
        parent, DlgProc, 0);
    return ret == IDOK;
}

void ScriptEditor::OnInit(HWND hDlg)
{
    SetDlgItemTextW(hDlg, IDC_EDIT_SCRIPT_NAME, s_scriptName.c_str());
    LoadCommands(hDlg);

    HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    EnumChildWindows(hDlg, [](HWND hChild, LPARAM lp) -> BOOL {
        SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
        }, (LPARAM)hFont);
}

void ScriptEditor::LoadCommands(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPT_CMDS);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    s_linePaths.clear();

    if (!s_actions) return;

    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"{");
    s_linePaths.push_back({ -1 });

    std::vector<std::wstring> lines;
    std::vector<std::vector<int>> paths;
    RenderActionWithPaths(*s_actions, {}, paths, lines, 1);

    for (size_t i = 0; i < lines.size(); i++) {
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)lines[i].c_str());
        s_linePaths.push_back(paths[i]);
    }

    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"}");
    s_linePaths.push_back({ -1 });
}

void ScriptEditor::OnAddCommand(HWND hDlg)
{
    if (!s_actions) return;

    ScriptAction action;
    action.type = ScriptActionType::RunCmd;
    action.enabled = true;

    if (!EditSingleAction(hDlg, action)) return;

    s_actions->push_back(action);
    LoadCommands(hDlg);
}

void ScriptEditor::OnEditCommand(HWND hDlg)
{
    if (!s_actions) return;

    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPT_CMDS);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_linePaths.size()) return;

    std::vector<int> path = s_linePaths[sel];
    if (path.empty() || path[0] < 0) return;

    ScriptAction* action = GetActionByPath(path);
    if (!action) return;

    if (!EditSingleAction(hDlg, *action)) return;

    LoadCommands(hDlg);
}

void ScriptEditor::OnDelCommand(HWND hDlg)
{
    if (!s_actions) return;

    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPT_CMDS);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_linePaths.size()) return;

    std::vector<int> path = s_linePaths[sel];
    if (path.empty() || path[0] < 0) return;

    RemoveActionByPath(path);
    LoadCommands(hDlg);
}

void ScriptEditor::OnMoveCommand(HWND hDlg, bool up)
{
    if (!s_actions) return;

    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPT_CMDS);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_linePaths.size()) return;

    std::vector<int> path = s_linePaths[sel];
    if (path.empty() || path[0] < 0) return;

    if (path.size() == 1) {
        // 顶层
        int idx = path[0];
        if (up) {
            if (idx <= 0) return;
            std::swap((*s_actions)[idx], (*s_actions)[idx - 1]);
        }
        else {
            if (idx >= (int)s_actions->size() - 1) return;
            std::swap((*s_actions)[idx], (*s_actions)[idx + 1]);
        }
    }
    else {
        // 块内
        ScriptAction* parent = GetActionByPath(
            std::vector<int>(path.begin(), path.end() - 1));
        if (!parent) return;
        int idx = path.back();
        if (up) {
            if (idx <= 0) return;
            std::swap(parent->children[idx], parent->children[idx - 1]);
        }
        else {
            if (idx >= (int)parent->children.size() - 1) return;
            std::swap(parent->children[idx], parent->children[idx + 1]);
        }
    }
    LoadCommands(hDlg);
}

void ScriptEditor::OnMoveIntoBlock(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPT_CMDS);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_linePaths.size()) return;

    std::vector<int> path = s_linePaths[sel];
    if (path.empty() || path[0] < 0) return;

    std::vector<int> blockPath;
    for (int i = sel - 1; i >= 0; i--) {
        std::vector<int> p = s_linePaths[i];
        if (p.empty() || p[0] < 0) continue;
        if (p.size() != path.size()) continue;

        if (p.back() < path.back()) {
            ScriptAction* candidate = GetActionByPath(p);
            if (candidate && IsBlockAction(candidate->type)) {
                blockPath = p;
                break;
            }
        }
    }

    if (blockPath.empty()) return;

    ScriptAction* action = GetActionByPath(path);
    if (!action) return;
    ScriptAction copy = *action;

    RemoveActionByPath(path);

    ScriptAction* block = GetActionByPath(blockPath);
    if (block) block->children.push_back(copy);

    LoadCommands(hDlg);
}

void ScriptEditor::OnMoveOutOfBlock(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_SCRIPT_CMDS);
    int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_linePaths.size()) return;

    std::vector<int> path = s_linePaths[sel];
    if (path.size() < 2) return;

    ScriptAction* action = GetActionByPath(path);
    if (!action) return;
    ScriptAction copy = *action;

    RemoveActionByPath(path);

    if (s_actions) s_actions->push_back(copy);

    LoadCommands(hDlg);
}

// ======================== 路径工具 ========================
ScriptAction* ScriptEditor::GetActionByPath(std::vector<int> path)
{
    if (!s_actions || path.empty()) return nullptr;
    if (path[0] < 0 || path[0] >= (int)s_actions->size()) return nullptr;

    ScriptAction* cur = &(*s_actions)[path[0]];
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] < 0 || path[i] >= (int)cur->children.size()) return nullptr;
        cur = &cur->children[path[i]];
    }
    return cur;
}

bool ScriptEditor::RemoveActionByPath(std::vector<int> path)
{
    if (!s_actions || path.empty()) return false;
    if (path[0] < 0 || path[0] >= (int)s_actions->size()) return false;

    if (path.size() == 1) {
        s_actions->erase(s_actions->begin() + path[0]);
        return true;
    }

    ScriptAction* parent = GetActionByPath(
        std::vector<int>(path.begin(), path.end() - 1));
    if (!parent) return false;
    int last = path.back();
    if (last < 0 || last >= (int)parent->children.size()) return false;
    parent->children.erase(parent->children.begin() + last);
    return true;
}

void ScriptEditor::OnSave(HWND hDlg)
{
    wchar_t name[256];
    GetDlgItemTextW(hDlg, IDC_EDIT_SCRIPT_NAME, name, 256);
    s_scriptName = name;
    EndDialog(hDlg, IDOK);
}

INT_PTR CALLBACK ScriptEditor::DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        OnInit(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_CMD_ADD:           OnAddCommand(hDlg); break;
        case IDC_BTN_CMD_EDIT:          OnEditCommand(hDlg); break;
        case IDC_BTN_CMD_DEL:           OnDelCommand(hDlg); break;
        case IDC_BTN_CMD_UP:            OnMoveCommand(hDlg, true); break;
        case IDC_BTN_CMD_DOWN:          OnMoveCommand(hDlg, false); break;
        case IDC_BTN_CMD_INTO_BLOCK:    OnMoveIntoBlock(hDlg); break;
        case IDC_BTN_CMD_OUTOF_BLOCK:   OnMoveOutOfBlock(hDlg); break;
        case IDC_BTN_SCRIPT_SAVE:       OnSave(hDlg); break;
        case IDC_BTN_SCRIPT_CANCEL:     EndDialog(hDlg, IDCANCEL); break;
        }
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}