#include "SHA256.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

std::wstring SHA256Hash(const std::wstring& input)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return L"";

    DWORD cbHash = 0, cbData = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(cbHash), &cbData, 0);

    std::vector<BYTE> hash(cbHash);
    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (BCRYPT_SUCCESS(status)) {
        BCryptHashData(hHash, (PUCHAR)input.c_str(),
            (ULONG)(input.size() * sizeof(wchar_t)), 0);
        BCryptFinishHash(hHash, hash.data(), cbHash, 0);
        BCryptDestroyHash(hHash);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::wstring result;
    for (BYTE b : hash) {
        wchar_t buf[4];
        swprintf_s(buf, L"%02x", b);
        result += buf;
    }
    return result;
}