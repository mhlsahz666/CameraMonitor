#include "Base64.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

static const char* B64_CHARS =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::vector<BYTE>& data)
{
    std::string out;
    int val = 0, valb = -6;
    for (BYTE c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(B64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(B64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::vector<BYTE> Base64Decode(const std::string& encoded)
{
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(BYTE)B64_CHARS[i]] = i;

    std::vector<BYTE> out;
    int val = 0, valb = -8;
    for (BYTE c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((BYTE)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::wstring ToWString(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

std::string ToString(const std::wstring& s)
{
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
        nullptr, 0, nullptr, nullptr);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
        &out[0], len, nullptr, nullptr);
    return out;
}