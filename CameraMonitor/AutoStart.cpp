#include "AutoStart.h"
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

const wchar_t* AutoStart::REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* CONFIG_REG_PATH = L"Software\\CameraMonitor";

StorageType g_storageType = StorageType::Registry;

// ======================== AutoStart ========================
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
std::wstring ConfigStore::GetConfigFilePath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p = path;
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos + 1);
    p += L"CameraMonitor.ini";
    return p;
}

bool ConfigStore::FileConfigExists()
{
    return GetFileAttributesW(GetConfigFilePath().c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ConfigStore::SaveToIni(const std::wstring& name, const std::wstring& value)
{
    return WritePrivateProfileStringW(L"Config", name.c_str(),
        value.c_str(), GetConfigFilePath().c_str()) != 0;
}

std::wstring ConfigStore::LoadFromIni(const std::wstring& name, const std::wstring& def)
{
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(L"Config", name.c_str(), def.c_str(),
        buf, 1024, GetConfigFilePath().c_str());
    return buf;
}

bool ConfigStore::SaveToRegistry(const std::wstring& name, const std::wstring& value, bool isString)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_REG_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    LONG r;
    if (isString) {
        r = RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
            (const BYTE*)value.c_str(),
            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    }
    else {
        int v = _wtoi(value.c_str());
        r = RegSetValueExW(hKey, name.c_str(), 0, REG_DWORD,
            (const BYTE*)&v, sizeof(v));
    }
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

std::wstring ConfigStore::LoadFromRegistry(const std::wstring& name, const std::wstring& def, bool isString)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_REG_PATH, 0,
        KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return def;
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(hKey, name.c_str(), NULL, &type, (LPBYTE)buf, &size);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS) return def;
    if (isString && type == REG_SZ) return buf;
    if (!isString && type == REG_DWORD) {
        wchar_t num[32];
        swprintf_s(num, L"%d", *(int*)buf);
        return num;
    }
    return def;
}

bool ConfigStore::SaveInt(const std::wstring& name, int value)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%d", value);
    if (g_storageType == StorageType::ExeDirectory)
        return SaveToIni(name, buf);
    return SaveToRegistry(name, buf, false);
}

int ConfigStore::LoadInt(const std::wstring& name, int defaultValue)
{
    std::wstring s;
    if (g_storageType == StorageType::ExeDirectory)
        s = LoadFromIni(name, L"");
    else
        s = LoadFromRegistry(name, L"", false);
    if (s.empty()) return defaultValue;
    return _wtoi(s.c_str());
}

bool ConfigStore::SaveString(const std::wstring& name, const std::wstring& value)
{
    if (g_storageType == StorageType::ExeDirectory)
        return SaveToIni(name, value);
    return SaveToRegistry(name, value, true);
}

std::wstring ConfigStore::LoadString(const std::wstring& name, const std::wstring& defaultValue)
{
    if (g_storageType == StorageType::ExeDirectory)
        return LoadFromIni(name, defaultValue);
    return LoadFromRegistry(name, defaultValue, true);
}

bool ConfigStore::SaveBool(const std::wstring& name, bool value)
{
    return SaveInt(name, value ? 1 : 0);
}

bool ConfigStore::LoadBool(const std::wstring& name, bool defaultValue)
{
    return LoadInt(name, defaultValue ? 1 : 0) != 0;
}