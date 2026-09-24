#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::string  Base64Encode(const std::vector<BYTE>& data);
std::vector<BYTE> Base64Decode(const std::string& encoded);
std::wstring ToWString(const std::string& s);
std::string  ToString(const std::wstring& s);