#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <string>

enum class StorageType { Registry, ExeDirectory };

extern StorageType g_storageType;

class AutoStart {
public:
    static bool Enable(const std::wstring& appName, const std::wstring& exePath);
    static bool Disable(const std::wstring& appName);
    static bool IsEnabled(const std::wstring& appName);

private:
    static const wchar_t* REG_RUN_PATH;
};

class ConfigStore {
public:
    static bool SaveInt(const std::wstring& name, int value);
    static int  LoadInt(const std::wstring& name, int defaultValue);
    static bool SaveString(const std::wstring& name, const std::wstring& value);
    static std::wstring LoadString(const std::wstring& name, const std::wstring& defaultValue);
    static bool SaveBool(const std::wstring& name, bool value);
    static bool LoadBool(const std::wstring& name, bool defaultValue);

    static std::wstring GetConfigFilePath();
    static bool FileConfigExists();

private:
    static bool SaveToIni(const std::wstring& name, const std::wstring& value);
    static std::wstring LoadFromIni(const std::wstring& name, const std::wstring& def);
    static bool SaveToRegistry(const std::wstring& name, const std::wstring& value, bool isString);
    static std::wstring LoadFromRegistry(const std::wstring& name, const std::wstring& def, bool isString);
};