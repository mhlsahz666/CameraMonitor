#include "AutoStart.h"

const wchar_t* AutoStart::REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* CONFIG_REG_PATH = L"Software\\CameraMonitor";

bool AutoStart::Enable(const std::wstring& appName, const std::wstring& exePath)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    LONG result = RegSetValueExW(hKey, appName.c_str(), 0, REG_SZ,
        (const BYTE*)exePath.c_str(),
        (DWORD)((exePath.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool AutoStart::Disable(const std::wstring& appName)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    LONG result = RegDeleteValueW(hKey, appName.c_str());
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool AutoStart::IsEnabled(const std::wstring& appName)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG result = RegQueryValueExW(hKey, appName.c_str(), NULL, &type, (LPBYTE)buf, &size);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS && type == REG_SZ;
}

// ======================== ConfigStore ========================
bool ConfigStore::SaveInt(const std::wstring& name, int value)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_REG_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(hKey, name.c_str(), 0, REG_DWORD,
        (const BYTE*)&value, sizeof(value));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

int ConfigStore::LoadInt(const std::wstring& name, int defaultValue)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_REG_PATH, 0,
        KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return defaultValue;
    DWORD value = 0, size = sizeof(value), type = 0;
    LONG r = RegQueryValueExW(hKey, name.c_str(), NULL, &type,
        (LPBYTE)&value, &size);
    RegCloseKey(hKey);
    if (r == ERROR_SUCCESS && type == REG_DWORD) return (int)value;
    return defaultValue;
}

bool ConfigStore::SaveString(const std::wstring& name, const std::wstring& value)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_REG_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
        (const BYTE*)value.c_str(),
        (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

std::wstring ConfigStore::LoadString(const std::wstring& name,
    const std::wstring& defaultValue)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_REG_PATH, 0,
        KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return defaultValue;
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(hKey, name.c_str(), NULL, &type,
        (LPBYTE)buf, &size);
    RegCloseKey(hKey);
    if (r == ERROR_SUCCESS && type == REG_SZ) return buf;
    return defaultValue;
}

bool ConfigStore::SaveBool(const std::wstring& name, bool value)
{
    return SaveInt(name, value ? 1 : 0);
}

bool ConfigStore::LoadBool(const std::wstring& name, bool defaultValue)
{
    return LoadInt(name, defaultValue ? 1 : 0) != 0;
}