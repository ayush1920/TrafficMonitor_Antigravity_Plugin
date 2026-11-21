#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wincred.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>

#include "QuotaSync.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

using json = nlohmann::json;

namespace QuotaSync
{
namespace
{
    std::string UnmaskBytes(const unsigned char* data, size_t len)
    {
        std::string out(len, '\0');
        for (size_t i = 0; i < len; ++i)
        {
            out[i] = static_cast<char>(data[i] ^ 0x5A);
        }
        return out;
    }

    std::string GetClientId()
    {
        static const unsigned char kCid[] = {
            107, 106, 109, 107, 106, 106, 108, 106, 108, 106, 111, 99, 107, 119, 46, 55, 50, 41,
            41, 51, 52, 104, 50, 104, 107, 54, 57, 40, 63, 104, 105, 111, 44, 46, 53, 54, 53, 48,
            50, 110, 61, 110, 106, 105, 63, 42, 116, 59, 42, 42, 41, 116, 61, 53, 53, 61, 54, 63,
            47, 41, 63, 40, 57, 53, 52, 46, 63, 52, 46, 116, 57, 53, 55
        };
        return UnmaskBytes(kCid, sizeof(kCid));
    }

    std::string GetClientSecret()
    {
        static const unsigned char kCsec[] = {
            29, 21, 25, 9, 10, 2, 119, 17, 111, 98, 28, 13, 8, 110, 98, 108, 22, 62, 22, 16,
            107, 55, 22, 24, 98, 41, 2, 25, 110, 32, 108, 43, 30, 27, 60
        };
        return UnmaskBytes(kCsec, sizeof(kCsec));
    }

    std::string ToLowerAscii(std::string s)
    {
        for (char& ch : s)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return s;
    }

    std::string UrlEncode(const std::string& val)
    {
        static const char kHex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(val.size() * 3);
        for (unsigned char c : val)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                out.push_back(static_cast<char>(c));
            }
            else
            {
                out.push_back('%');
                out.push_back(kHex[(c >> 4) & 0xF]);
                out.push_back(kHex[c & 0xF]);
            }
        }
        return out;
    }

    std::string UrlDecode(const std::string& val)
    {
        std::string out;
        out.reserve(val.size());
        for (size_t i = 0; i < val.size(); ++i)
        {
            if (val[i] == '%' && i + 2 < val.size() &&
                std::isxdigit(static_cast<unsigned char>(val[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(val[i + 2])))
            {
                int hi = std::isdigit(static_cast<unsigned char>(val[i + 1])) ? (val[i + 1] - '0') : (std::toupper(static_cast<unsigned char>(val[i + 1])) - 'A' + 10);
                int lo = std::isdigit(static_cast<unsigned char>(val[i + 2])) ? (val[i + 2] - '0') : (std::toupper(static_cast<unsigned char>(val[i + 2])) - 'A' + 10);
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            }
            else if (val[i] == '+')
            {
                out.push_back(' ');
            }
            else
            {
                out.push_back(val[i]);
            }
        }
        return out;
    }

    std::string Base64UrlEncode(const unsigned char* data, size_t len)
    {
        DWORD outLen = 0;
        if (!CryptBinaryToStringA(data, static_cast<DWORD>(len), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen) || outLen == 0)
        {
            return "";
        }
        std::string b64(outLen, '\0');
        if (!CryptBinaryToStringA(data, static_cast<DWORD>(len), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64[0], &outLen))
        {
            return "";
        }
        while (!b64.empty() && (b64.back() == '\0' || b64.back() == '='))
        {
            b64.pop_back();
        }
        for (char& ch : b64)
        {
            if (ch == '+') ch = '-';
            else if (ch == '/') ch = '_';
        }
        return b64;
    }

    std::string Base64UrlDecode(const std::string& input)
    {
        std::string b64 = input;
        for (char& ch : b64)
        {
            if (ch == '-') ch = '+';
            else if (ch == '_') ch = '/';
        }
        while (b64.size() % 4 != 0) b64.push_back('=');

        DWORD outLen = 0;
        if (!CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, nullptr, &outLen, nullptr, nullptr) || outLen == 0)
        {
            return "";
        }
        std::string out(outLen, '\0');
        if (!CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(&out[0]), &outLen, nullptr, nullptr))
        {
            return "";
        }
        out.resize(outLen);
        return out;
    }

    std::string RandomGuid8()
    {
        unsigned char bytes[4] = {};
        if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        {
            DWORD t = GetTickCount();
            memcpy(bytes, &t, sizeof(bytes));
        }
        char buf[16] = {};
        snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", bytes[0], bytes[1], bytes[2], bytes[3]);
        return std::string(buf);
    }

    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0) return L"";
        std::wstring w(needed, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], needed);
        return w;
    }

    class ScopedVaultMutex
    {
    public:
        ScopedVaultMutex()
        {
            m_hMutex = CreateMutexW(nullptr, FALSE, L"Local\\AntigravityQuotaVaultMutex");
            if (m_hMutex)
            {
                DWORD r = WaitForSingleObject(m_hMutex, 2500);
                m_locked = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
            }
        }
        ~ScopedVaultMutex()
        {
            if (m_hMutex)
            {
                if (m_locked) ReleaseMutex(m_hMutex);
                CloseHandle(m_hMutex);
            }
        }
    private:
        HANDLE m_hMutex{nullptr};
        bool m_locked{false};
    };

    std::wstring GetKnownFolder(int csidl, const wchar_t* envFallback)
    {
        wchar_t buf[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf)) && buf[0] != L'\0')
        {
            return std::wstring(buf);
        }
        DWORD len = GetEnvironmentVariableW(envFallback, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            return std::wstring(buf);
        }
        return L"";
    }

    std::wstring ResolveUserGeminiTokenPath()
    {
        std::wstring home = GetKnownFolder(CSIDL_PROFILE, L"USERPROFILE");
        if (home.empty()) return L"";
        return home + L"\\.gemini\\jetski-standalone-oauth-token";
    }

    void EnsureDirectoryExists(const std::wstring& dirPath)
    {
        if (dirPath.empty()) return;
        SHCreateDirectoryExW(nullptr, dirPath.c_str(), nullptr);
    }

    bool ReadFileBytes(const std::wstring& path, std::vector<BYTE>& outBytes)
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        DWORD sz = GetFileSize(hFile, nullptr);
        if (sz == 0 || sz == INVALID_FILE_SIZE)
        {
            CloseHandle(hFile);
            return false;
        }
        outBytes.resize(sz);
        DWORD rd = 0;
        bool ok = (ReadFile(hFile, outBytes.data(), sz, &rd, nullptr) && rd == sz);
        CloseHandle(hFile);
        return ok;
    }

    bool WriteVaultJsonInternal(const std::string& jsonStr)
    {
        std::wstring path = ResolveVaultPath();
        if (path.empty()) return false;

        size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            EnsureDirectoryExists(path.substr(0, slash));
        }

        DATA_BLOB inBlob;
        inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(jsonStr.data()));
        inBlob.cbData = static_cast<DWORD>(jsonStr.size());

        DATA_BLOB outBlob = {};
        if (!CryptProtectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob))
        {
            return false;
        }

        std::wstring tmpPath = path + L".tmp";
        HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool writeOk = false;
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            if (WriteFile(hFile, outBlob.pbData, outBlob.cbData, &written, nullptr) && written == outBlob.cbData)
            {
                FlushFileBuffers(hFile);
                writeOk = true;
            }
            CloseHandle(hFile);
        }
        LocalFree(outBlob.pbData);

        if (!writeOk)
        {
            DeleteFileW(tmpPath.c_str());
            return false;
        }

        if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tmpPath.c_str());
            return false;
        }
        return true;
    }

    bool ReadVaultJsonUnlocked(std::string& outJson)
    {
        std::wstring path = ResolveVaultPath();
        std::vector<BYTE> enc;
        if (!ReadFileBytes(path, enc)) return false;

        DATA_BLOB inBlob;
        inBlob.pbData = enc.data();
        inBlob.cbData = static_cast<DWORD>(enc.size());
        DATA_BLOB outBlob = {};
        if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob))
        {
            return false;
        }
        if (outBlob.pbData && outBlob.cbData > 0)
        {
            outJson.assign(reinterpret_cast<const char*>(outBlob.pbData), outBlob.cbData);
        }
        LocalFree(outBlob.pbData);
        return !outJson.empty();
    }

    struct HttpResponse
    {
        DWORD statusCode{0};
        std::string body;
        bool networkOk{false};
    };

    HttpResponse HttpRequest(const wchar_t* method,
                             const wchar_t* host,
                             INTERNET_PORT port,
                             const wchar_t* path,
                             const std::wstring& extraHeaders,
                             const std::string& body,
                             std::atomic<bool>* pStopFlag = nullptr)
    {
        HttpResponse res;
        if (pStopFlag && pStopFlag->load(std::memory_order_relaxed)) return res;

        HINTERNET hSession = WinHttpOpen(L"antigravity/hub/2.34.0",
                                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession)
        {
            hSession = WinHttpOpen(L"antigravity/hub/2.34.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!hSession) return res;

        WinHttpSetTimeouts(hSession, 5000, 8000, 8000, 10000);

        HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return res;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path, nullptr,
                                                WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return res;
        }

        if (!extraHeaders.empty())
        {
            WinHttpAddRequestHeaders(hRequest, extraHeaders.c_str(), static_cast<DWORD>(-1L),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        BOOL sent = WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0);

        if (sent && (!pStopFlag || !pStopFlag->load(std::memory_order_relaxed)) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
            res.statusCode = statusCode;
            res.networkOk = true;

            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
            {
                if (pStopFlag && pStopFlag->load(std::memory_order_relaxed)) break;
                std::string chunk(avail, '\0');
                DWORD readBytes = 0;
                if (WinHttpReadData(hRequest, &chunk[0], avail, &readBytes) && readBytes > 0)
                {
                    chunk.resize(readBytes);
                    res.body.append(chunk);
                }
                else
                {
                    break;
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }

    std::string ExtractEmailFromIdToken(const std::string& idToken)
    {
        if (idToken.empty()) return "";
        size_t dot1 = idToken.find('.');
        if (dot1 == std::string::npos) return "";
        size_t dot2 = idToken.find('.', dot1 + 1);
        if (dot2 == std::string::npos || dot2 <= dot1 + 1) return "";

        std::string payloadJson = Base64UrlDecode(idToken.substr(dot1 + 1, dot2 - dot1 - 1));
        if (payloadJson.empty()) return "";

        json pj = json::parse(payloadJson, nullptr, false);
        if (!pj.is_discarded() && pj.is_object() && pj.contains("email") && pj["email"].is_string())
        {
            return pj["email"].get<std::string>();
        }
        return "";
    }

    std::string FetchEmailFromUserInfo(const std::string& accessToken, std::atomic<bool>* pStopFlag)
    {
        if (accessToken.empty()) return "";
        std::wstring hdr = L"Authorization: Bearer " + Utf8ToWide(accessToken) + L"\r\nUser-Agent: google-antigravity/2.25.0\r\n";
        HttpResponse r = HttpRequest(L"GET", L"www.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT,
                                     L"/oauth2/v1/userinfo?alt=json", hdr, "", pStopFlag);
        if (r.networkOk && r.statusCode >= 200 && r.statusCode < 300)
        {
            json dj = json::parse(r.body, nullptr, false);
            if (!dj.is_discarded() && dj.is_object() && dj.contains("email") && dj["email"].is_string())
            {
                return dj["email"].get<std::string>();
            }
        }
        return "";
    }

    std::string BuildIdeRawBlob(const std::string& accessToken,
                                const std::string& refreshToken,
                                const std::string& idToken,
                                long long expiresAtUtc)
    {
        time_t t = static_cast<time_t>(expiresAtUtc);
        struct tm gmt = {};
        gmtime_s(&gmt, &t);
        char expBuf[32] = {};
        snprintf(expBuf, sizeof(expBuf), "%04d-%02d-%02dT%02d:%02d:%02d+00:00",
                 gmt.tm_year + 1900, gmt.tm_mon + 1, gmt.tm_mday,
                 gmt.tm_hour, gmt.tm_min, gmt.tm_sec);

        json root = {
            {"token", {
                {"access_token", accessToken},
                {"token_type", "Bearer"},
                {"refresh_token", refreshToken},
                {"expiry", std::string(expBuf)}
            }},
            {"auth_method", "consumer"},
            {"id_token", idToken}
        };
        return root.dump();
    }

    void SyncActiveTokenToIde(const json& acc)
    {
        if (!acc.value("IsActive", false)) return;
        std::string rawBlob = acc.value("RawBlob", std::string(""));
        if (rawBlob.empty()) return;

        std::wstring tokenPath = ResolveUserGeminiTokenPath();
        if (!tokenPath.empty())
        {
            size_t slash = tokenPath.find_last_of(L"\\/");
            if (slash != std::wstring::npos) EnsureDirectoryExists(tokenPath.substr(0, slash));
            HANDLE hFile = CreateFileW(tokenPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile(hFile, rawBlob.data(), static_cast<DWORD>(rawBlob.size()), &written, nullptr);
                CloseHandle(hFile);
            }
        }

        CREDENTIALW cred = {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<LPWSTR>(L"gemini:antigravity");
        cred.UserName = const_cast<LPWSTR>(L"antigravity");
        cred.CredentialBlobSize = static_cast<DWORD>(rawBlob.size());
        cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(rawBlob.data()));
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        CredWriteW(&cred, 0);
    }

    json MakeDefaultAccountRecord(const std::string& email,
                                  const std::string& label,
                                  const std::string& accessToken,
                                  const std::string& refreshToken,
                                  const std::string& idToken,
                                  long long expiresAt,
                                  const std::string& rawBlob,
                                  bool isActive)
    {
        std::string resolvedLabel = label;
        if (resolvedLabel.empty() && !email.empty())
        {
            size_t at = email.find('@');
            resolvedLabel = (at != std::string::npos) ? email.substr(0, at) : email;
        }
        if (resolvedLabel.empty()) resolvedLabel = "Default";

        return json{
            {"Id", RandomGuid8()},
            {"Email", email},
            {"Label", resolvedLabel},
            {"ProjectId", "rising-fact-p41fc"},
            {"IsActive", isActive},
            {"IsVisible", true},
            {"AuthFailed", false},
            {"AccessToken", accessToken},
            {"RefreshToken", refreshToken},
            {"IdToken", idToken},
            {"ExpiresAt", expiresAt},
            {"RawBlob", rawBlob},
            {"HasGemini", true},
            {"HasClaude", true},
            {"GFiveH", 1.0},
            {"GWeekly", 1.0},
            {"GFiveHReset", "--"},
            {"GWeeklyReset", "--"},
            {"CFiveH", 1.0},
            {"CWeekly", 1.0},
            {"CFiveHReset", "--"},
            {"CWeeklyReset", "--"},
            {"HasArgon", false},
            {"AWeekly", 1.0},
            {"AWeeklyReset", "--"}
        };
    }

    // Returns: 1 = refreshed ok, 0 = still valid / skipped, -1 = refresh rejected / missing token, -2 = network error
    int RefreshAccountIfNeeded(json& acc, bool force, std::atomic<bool>* pStopFlag)
    {
        long long nowUtc = static_cast<long long>(std::time(nullptr));
        long long expiresAt = acc.value("ExpiresAt", 0LL);
        std::string accessToken = acc.value("AccessToken", std::string(""));
        std::string refreshToken = acc.value("RefreshToken", std::string(""));

        if (!force && !accessToken.empty() && expiresAt > nowUtc + 120)
        {
            return 0;
        }
        if (refreshToken.empty())
        {
            if (expiresAt <= nowUtc || accessToken.empty())
            {
                acc["AuthFailed"] = true;
                return -1;
            }
            return 0;
        }

        std::string formBody =
            "client_id=" + UrlEncode(GetClientId()) +
            "&client_secret=" + UrlEncode(GetClientSecret()) +
            "&refresh_token=" + UrlEncode(refreshToken) +
            "&grant_type=refresh_token";

        std::wstring hdr = L"Content-Type: application/x-www-form-urlencoded\r\nUser-Agent: antigravity/hub/2.34.0\r\n";
        HttpResponse resp = HttpRequest(L"POST", L"oauth2.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT,
                                        L"/token", hdr, formBody, pStopFlag);
        if (!resp.networkOk)
        {
            return -2;
        }
        if (resp.statusCode < 200 || resp.statusCode >= 300)
        {
            acc["AuthFailed"] = true;
            return -1;
        }

        json tok = json::parse(resp.body, nullptr, false);
        if (tok.is_discarded() || !tok.is_object() || !tok.contains("access_token") || !tok["access_token"].is_string())
        {
            acc["AuthFailed"] = true;
            return -1;
        }

        std::string newAccess = tok["access_token"].get<std::string>();
        long long expiresIn = tok.value("expires_in", 3600LL);
        long long newExp = nowUtc + expiresIn;
        std::string newRefresh = tok.value("refresh_token", std::string(""));
        if (newRefresh.empty()) newRefresh = refreshToken;
        std::string newIdTok = tok.value("id_token", std::string(""));
        if (newIdTok.empty()) newIdTok = acc.value("IdToken", std::string(""));

        if (acc.value("Email", std::string("")).empty() && !newIdTok.empty())
        {
            std::string extracted = ExtractEmailFromIdToken(newIdTok);
            if (!extracted.empty()) acc["Email"] = extracted;
        }

        acc["AccessToken"] = newAccess;
        acc["RefreshToken"] = newRefresh;
        acc["IdToken"] = newIdTok;
        acc["ExpiresAt"] = newExp;
        acc["RawBlob"] = BuildIdeRawBlob(newAccess, newRefresh, newIdTok, newExp);
        acc["AuthFailed"] = false;
        SyncActiveTokenToIde(acc);
        return 1;
    }

    // Returns: 1 = fetched ok, -1 = auth error (401/403), -2 = network/parse error
    int FetchCloudCodeForAccount(json& acc, std::atomic<bool>* pStopFlag)
    {
        std::string accessToken = acc.value("AccessToken", std::string(""));
        if (accessToken.empty())
        {
            acc["AuthFailed"] = true;
            return -1;
        }

        auto doPost = [&](const std::string& tok) -> HttpResponse {
            std::wstring hdr =
                L"Content-Type: application/json\r\n"
                L"Authorization: Bearer " + Utf8ToWide(tok) + L"\r\n"
                L"User-Agent: antigravity/hub/2.34.0\r\n";
            return HttpRequest(L"POST", L"daily-cloudcode-pa.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT,
                               L"/v1internal:retrieveUserQuotaSummary", hdr, "{}", pStopFlag);
        };

        HttpResponse resp = doPost(accessToken);
        if (!resp.networkOk) return -2;

        if (resp.statusCode == 401 || resp.statusCode == 403)
        {
            int r = RefreshAccountIfNeeded(acc, true, pStopFlag);
            if (r == 1)
            {
                resp = doPost(acc.value("AccessToken", std::string("")));
                if (!resp.networkOk) return -2;
            }
            else if (r == -2)
            {
                return -2;
            }
            else
            {
                acc["AuthFailed"] = true;
                return -1;
            }
        }

        if (resp.statusCode == 401 || resp.statusCode == 403)
        {
            acc["AuthFailed"] = true;
            return -1;
        }
        if (resp.statusCode < 200 || resp.statusCode >= 300)
        {
            return -2;
        }

        double g5h = acc.value("GFiveH", 1.0);
        double gWk = acc.value("GWeekly", 1.0);
        std::string g5hR = acc.value("GFiveHReset", std::string("--"));
        std::string gWkR = acc.value("GWeeklyReset", std::string("--"));
        double c5h = acc.value("CFiveH", 1.0);
        double cWk = acc.value("CWeekly", 1.0);
        std::string c5hR = acc.value("CFiveHReset", std::string("--"));
        std::string cWkR = acc.value("CWeeklyReset", std::string("--"));
        bool hasArgon = false;
        double aWk = acc.value("AWeekly", 1.0);
        std::string aWkR = acc.value("AWeeklyReset", std::string("--"));

        if (!ApplyQuotaSummaryJson(resp.body, g5h, g5hR, gWk, gWkR, c5h, c5hR, cWk, cWkR, hasArgon, aWk, aWkR, 0))
        {
            return -2;
        }

        acc["GFiveH"] = g5h;
        acc["GFiveHReset"] = g5hR;
        acc["GWeekly"] = gWk;
        acc["GWeeklyReset"] = gWkR;
        acc["CFiveH"] = c5h;
        acc["CFiveHReset"] = c5hR;
        acc["CWeekly"] = cWk;
        acc["CWeeklyReset"] = cWkR;
        acc["HasArgon"] = hasArgon;
        acc["AWeekly"] = aWk;
        acc["AWeeklyReset"] = aWkR;
        acc["AuthFailed"] = false;
        return 1;
    }

    int FindActiveAccountIndex(const json& arr)
    {
        if (!arr.is_array() || arr.empty()) return -1;
        for (size_t i = 0; i < arr.size(); ++i)
        {
            if (arr[i].is_object() && arr[i].value("IsActive", false))
            {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    bool ParseIdeBlobToAccount(const std::string& blobStr, json& outAcc)
    {
        json j = json::parse(blobStr, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("token") || !j["token"].is_object())
        {
            return false;
        }
        const json& tok = j["token"];
        std::string accessToken = tok.value("access_token", std::string(""));
        std::string refreshToken = tok.value("refresh_token", std::string(""));
        if (accessToken.empty() && refreshToken.empty()) return false;

        std::string expiryIso = tok.value("expiry", std::string(""));
        long long expUtc = ParseIsoUtcEpoch(expiryIso);
        std::string idToken = j.value("id_token", std::string(""));
        std::string email = j.value("id_token_email", std::string(""));
        if (email.empty()) email = ExtractEmailFromIdToken(idToken);

        outAcc = MakeDefaultAccountRecord(email, "", accessToken, refreshToken, idToken, expUtc, blobStr, true);
        return true;
    }

    std::string Sha256Base64Url(const std::string& ascii)
    {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        unsigned char digest[32] = {};
        bool ok = false;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
        {
            if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0)
            {
                if (BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(ascii.data())),
                                   static_cast<ULONG>(ascii.size()), 0) == 0 &&
                    BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0)
                {
                    ok = true;
                }
                BCryptDestroyHash(hHash);
            }
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
        return ok ? Base64UrlEncode(digest, sizeof(digest)) : "";
    }
} // namespace

std::wstring ResolveVaultPath()
{
    std::wstring localApp = GetKnownFolder(CSIDL_LOCAL_APPDATA, L"LOCALAPPDATA");
    if (localApp.empty()) return L"";
    return localApp + L"\\AntigravityQuota\\vault.dat";
}

bool ReadVaultJson(std::string& outJson)
{
    ScopedVaultMutex lock;
    return ReadVaultJsonUnlocked(outJson);
}

long long ParseIsoUtcEpoch(const std::string& iso)
{
    if (iso.size() < 19) return 0;
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    if (sscanf_s(iso.c_str(), "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) != 6)
    {
        return 0;
    }
    struct tm t = {};
    t.tm_year = yr - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = dy;
    t.tm_hour = hr;
    t.tm_min = mn;
    t.tm_sec = sc;
    time_t baseUtc = _mkgmtime(&t);
    if (baseUtc == static_cast<time_t>(-1)) return 0;

    // Handle explicit +/-HH:MM timezone offset after 'T' (skip YYYY-MM-DD at index 0..9)
    size_t tzPos = iso.find_first_of("+-Z", 19);
    if (tzPos != std::string::npos && (iso[tzPos] == '+' || iso[tzPos] == '-'))
    {
        int tzH = 0, tzM = 0;
        if (sscanf_s(iso.c_str() + tzPos + 1, "%d:%d", &tzH, &tzM) >= 1)
        {
            long long offsetSec = static_cast<long long>(tzH) * 3600LL + static_cast<long long>(tzM) * 60LL;
            if (iso[tzPos] == '+') return static_cast<long long>(baseUtc) - offsetSec;
            else return static_cast<long long>(baseUtc) + offsetSec;
        }
    }
    return static_cast<long long>(baseUtc);
}

std::string FormatResetDuration(long long targetEpochUtc, long long nowEpochUtc)
{
    if (targetEpochUtc <= 0) return "--";
    if (nowEpochUtc <= 0) nowEpochUtc = static_cast<long long>(std::time(nullptr));
    long long diff = targetEpochUtc - nowEpochUtc;
    if (diff <= 0) return "Ready";

    long long days = diff / 86400;
    long long hours = (diff % 86400) / 3600;
    long long mins = (diff % 3600) / 60;
    char buf[32] = {};
    if (days > 0)
    {
        snprintf(buf, sizeof(buf), "%lldd %02lldh", days, hours);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%lldh %02lldm", hours, mins);
    }
    return std::string(buf);
}

bool ApplyQuotaSummaryJson(const std::string& summaryJson,
                           double& g5h, std::string& g5hReset,
                           double& gWk, std::string& gWkReset,
                           double& c5h, std::string& c5hReset,
                           double& cWk, std::string& cWkReset,
                           bool& hasArgon, double& aWk, std::string& aWkReset,
                           long long nowEpochUtc)
{
    json j = json::parse(summaryJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;

    const json* pRoot = &j;
    if (j.contains("response") && j["response"].is_object())
    {
        pRoot = &j["response"];
    }
    if (!pRoot->contains("groups") || !(*pRoot)["groups"].is_array())
    {
        return false;
    }

    hasArgon = false;
    for (const auto& g : (*pRoot)["groups"])
    {
        if (!g.is_object()) continue;
        std::string disp = ToLowerAscii(g.value("displayName", std::string("")));
        bool isA = (disp.find("argon") != std::string::npos);
        bool isG = (!isA && disp.find("gemini") != std::string::npos);
        bool isC = (disp.find("claude") != std::string::npos ||
                    disp.find("opus") != std::string::npos ||
                    disp.find("gpt") != std::string::npos);
        if ((!isA && !isG && !isC) || !g.contains("buckets") || !g["buckets"].is_array()) continue;

        for (const auto& b : g["buckets"])
        {
            if (!b.is_object() || !b.contains("remainingFraction") || !b["remainingFraction"].is_number()) continue;
            double f = b["remainingFraction"].get<double>();
            std::string win = b.contains("window") && b["window"].is_string()
                                  ? b["window"].get<std::string>()
                                  : b.value("displayName", std::string(""));
            win = ToLowerAscii(win);
            std::string rstIso = b.value("resetTime", std::string(""));
            std::string rst = FormatResetDuration(ParseIsoUtcEpoch(rstIso), nowEpochUtc);
            bool wk = (win.find("week") != std::string::npos ||
                       win.find("wk") != std::string::npos ||
                       win.find("7d") != std::string::npos);

            if (isA) { hasArgon = true; aWk = f; aWkRst:; aWkReset = rst; }
            if (isG) { if (wk) { gWk = f; gWkReset = rst; } else { g5h = f; g5hReset = rst; } }
            if (isC) { if (wk) { cWk = f; cWkReset = rst; } else { c5h = f; c5hReset = rst; } }
        }
    }
    return true;
}

bool TryBootstrapFromIde(std::string& outVaultJson)
{
    std::string blob;
    std::wstring tokPath = ResolveUserGeminiTokenPath();
    if (!tokPath.empty())
    {
        std::vector<BYTE> bytes;
        if (ReadFileBytes(tokPath, bytes))
        {
            blob.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    }

    if (blob.empty())
    {
        PCREDENTIALW pCred = nullptr;
        if (CredReadW(L"gemini:antigravity", CRED_TYPE_GENERIC, 0, &pCred) && pCred)
        {
            if (pCred->CredentialBlob && pCred->CredentialBlobSize > 0)
            {
                blob.assign(reinterpret_cast<const char*>(pCred->CredentialBlob), pCred->CredentialBlobSize);
            }
            CredFree(pCred);
        }
    }

    if (blob.empty()) return false;

    json acc;
    if (!ParseIdeBlobToAccount(blob, acc)) return false;

    ScopedVaultMutex lock;
    std::string existing;
    if (ReadVaultJsonUnlocked(existing))
    {
        json arr = json::parse(existing, nullptr, false);
        if (arr.is_array() && !arr.empty())
        {
            outVaultJson = existing;
            return true;
        }
    }

    json newArr = json::array();
    newArr.push_back(acc);
    std::string serialized = newArr.dump();
    if (!WriteVaultJsonInternal(serialized)) return false;
    outVaultJson = serialized;
    return true;
}

bool PollActiveAccountAndUpdateVault(std::string& outUpdatedVaultJson, std::atomic<bool>* pStopFlag)
{
    std::string vaultStr;
    {
        ScopedVaultMutex lock;
        ReadVaultJsonUnlocked(vaultStr);
    }

    json arr = json::parse(vaultStr, nullptr, false);
    if (arr.is_discarded() || !arr.is_array() || arr.empty())
    {
        if (!TryBootstrapFromIde(vaultStr))
        {
            return false;
        }
        arr = json::parse(vaultStr, nullptr, false);
        if (arr.is_discarded() || !arr.is_array() || arr.empty()) return false;
    }

    int idx = FindActiveAccountIndex(arr);
    if (idx < 0 || !arr[idx].is_object()) return false;

    json acc = arr[idx];
    std::string targetEmail = acc.value("Email", std::string(""));
    std::string targetId = acc.value("Id", std::string(""));

    int refRes = RefreshAccountIfNeeded(acc, false, pStopFlag);
    if (refRes == -2)
    {
        // Transient network error while refreshing: keep existing data intact
        return false;
    }

    if (refRes != -1)
    {
        FetchCloudCodeForAccount(acc, pStopFlag);
    }

    if (pStopFlag && pStopFlag->load(std::memory_order_relaxed)) return false;

    // Re-read vault under mutex to preserve any concurrent edits (visibility/reordering/new accounts)
    ScopedVaultMutex lock;
    std::string latestStr;
    json latestArr;
    if (ReadVaultJsonUnlocked(latestStr))
    {
        latestArr = json::parse(latestStr, nullptr, false);
    }
    if (latestArr.is_discarded() || !latestArr.is_array() || latestArr.empty())
    {
        latestArr = arr;
    }

    int writeIdx = -1;
    for (size_t i = 0; i < latestArr.size(); ++i)
    {
        if (!latestArr[i].is_object()) continue;
        std::string e = latestArr[i].value("Email", std::string(""));
        std::string id = latestArr[i].value("Id", std::string(""));
        if ((!targetEmail.empty() && _stricmp(e.c_str(), targetEmail.c_str()) == 0) ||
            (!targetId.empty() && id == targetId))
        {
            writeIdx = static_cast<int>(i);
            break;
        }
    }
    if (writeIdx < 0)
    {
        writeIdx = FindActiveAccountIndex(latestArr);
    }
    if (writeIdx < 0 || !latestArr[writeIdx].is_object()) return false;

    // Merge updated tokens and quotas while preserving user's IsVisible / IsActive / Label choices
    json& dst = latestArr[writeIdx];
    dst["AccessToken"] = acc["AccessToken"];
    dst["RefreshToken"] = acc["RefreshToken"];
    dst["IdToken"] = acc["IdToken"];
    dst["ExpiresAt"] = acc["ExpiresAt"];
    dst["RawBlob"] = acc["RawBlob"];
    dst["AuthFailed"] = acc["AuthFailed"];
    if (dst.value("Email", std::string("")).empty() && !acc.value("Email", std::string("")).empty())
    {
        dst["Email"] = acc["Email"];
    }
    dst["GFiveH"] = acc["GFiveH"];
    dst["GFiveHReset"] = acc["GFiveHReset"];
    dst["GWeekly"] = acc["GWeekly"];
    dst["GWeeklyReset"] = acc["GWeeklyReset"];
    dst["CFiveH"] = acc["CFiveH"];
    dst["CFiveHReset"] = acc["CFiveHReset"];
    dst["CWeekly"] = acc["CWeekly"];
    dst["CWeeklyReset"] = acc["CWeeklyReset"];
    dst["HasArgon"] = acc["HasArgon"];
    dst["AWeekly"] = acc["AWeekly"];
    dst["AWeeklyReset"] = acc["AWeeklyReset"];

    std::string finalStr = latestArr.dump();
    WriteVaultJsonInternal(finalStr);
    outUpdatedVaultJson = finalStr;
    return true;
}

bool SignInWithBrowser(std::string& outUpdatedVaultJson, std::string& outErrorMsg, std::atomic<bool>* pStopFlag)
{
    unsigned char randBytes[48] = {};
    if (BCryptGenRandom(nullptr, randBytes, sizeof(randBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    {
        outErrorMsg = "Failed to generate PKCE random entropy.";
        return false;
    }
    std::string verifier = Base64UrlEncode(randBytes, sizeof(randBytes));
    std::string challenge = Sha256Base64Url(verifier);
    if (verifier.empty() || challenge.empty())
    {
        outErrorMsg = "Failed to compute PKCE challenge.";
        return false;
    }

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        outErrorMsg = "Failed to initialize Winsock.";
        return false;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET)
    {
        WSACleanup();
        outErrorMsg = "Failed to create loopback socket.";
        return false;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(51121);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(listenSock, 4) == SOCKET_ERROR)
    {
        closesocket(listenSock);
        WSACleanup();
        outErrorMsg = "Port 51121 is currently in use by another authentication window.";
        return false;
    }

    const std::string redirectUri = "http://localhost:51121/oauth-callback";
    const std::string scopes =
        "https://www.googleapis.com/auth/cloud-platform "
        "https://www.googleapis.com/auth/userinfo.email "
        "https://www.googleapis.com/auth/userinfo.profile "
        "https://www.googleapis.com/auth/cclog "
        "https://www.googleapis.com/auth/experimentsandconfigs";

    std::string authUrl =
        "https://accounts.google.com/o/oauth2/v2/auth?client_id=" + UrlEncode(GetClientId()) +
        "&response_type=code&redirect_uri=" + UrlEncode(redirectUri) +
        "&scope=" + UrlEncode(scopes) +
        "&code_challenge=" + UrlEncode(challenge) +
        "&code_challenge_method=S256&access_type=offline&prompt=consent";

    std::wstring wAuthUrl = Utf8ToWide(authUrl);
    ShellExecuteW(nullptr, L"open", wAuthUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    std::string authCode;
    ULONGLONG deadline = GetTickCount64() + 120000ULL;
    while (GetTickCount64() < deadline && authCode.empty())
    {
        if (pStopFlag && pStopFlag->load(std::memory_order_relaxed)) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenSock, &fds);
        timeval tv = { 0, 250000 }; // 250ms
        int sel = select(0, &fds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        SOCKET clientSock = accept(listenSock, nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) continue;

        char reqBuf[4096] = {};
        int rd = recv(clientSock, reqBuf, sizeof(reqBuf) - 1, 0);
        if (rd > 0)
        {
            std::string req(reqBuf, rd);
            size_t getPos = req.find("GET /oauth-callback?");
            if (getPos != std::string::npos)
            {
                size_t qStart = getPos + 20;
                size_t qEnd = req.find(' ', qStart);
                if (qEnd != std::string::npos)
                {
                    std::string query = req.substr(qStart, qEnd - qStart);
                    size_t pos = 0;
                    while (pos < query.size())
                    {
                        size_t amp = query.find('&', pos);
                        std::string pair = (amp == std::string::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
                        size_t eq = pair.find('=');
                        if (eq != std::string::npos && pair.substr(0, eq) == "code")
                        {
                            authCode = UrlDecode(pair.substr(eq + 1));
                            break;
                        }
                        if (amp == std::string::npos) break;
                        pos = amp + 1;
                    }
                }

                const std::string html =
                    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Antigravity Linked</title></head>"
                    "<body style='background:#0d1117;color:#3fb950;font-family:Segoe UI,sans-serif;text-align:center;padding-top:15%'>"
                    "<h2>&#10003; Antigravity Account Linked</h2>"
                    "<p style='color:#8b949e;font-size:15px'>You may close this tab and return to TrafficMonitor.</p>"
                    "</body></html>";
                std::string httpResp =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                    std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
                send(clientSock, httpResp.data(), static_cast<int>(httpResp.size()), 0);
            }
            else
            {
                const std::string notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(clientSock, notFound.data(), static_cast<int>(notFound.size()), 0);
            }
        }
        closesocket(clientSock);
    }

    closesocket(listenSock);
    WSACleanup();

    if (authCode.empty())
    {
        outErrorMsg = "Sign-in timed out or was cancelled.";
        return false;
    }

    std::string exchBody =
        "client_id=" + UrlEncode(GetClientId()) +
        "&client_secret=" + UrlEncode(GetClientSecret()) +
        "&code=" + UrlEncode(authCode) +
        "&grant_type=authorization_code" +
        "&redirect_uri=" + UrlEncode(redirectUri) +
        "&code_verifier=" + UrlEncode(verifier);

    std::wstring hdr = L"Content-Type: application/x-www-form-urlencoded\r\nUser-Agent: antigravity/hub/2.34.0\r\n";
    HttpResponse tokResp = HttpRequest(L"POST", L"oauth2.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT,
                                       L"/token", hdr, exchBody, pStopFlag);
    if (!tokResp.networkOk || tokResp.statusCode < 200 || tokResp.statusCode >= 300)
    {
        outErrorMsg = "Failed to exchange authorization code for Google tokens.";
        return false;
    }

    json tok = json::parse(tokResp.body, nullptr, false);
    if (tok.is_discarded() || !tok.is_object() || !tok.contains("access_token") || !tok["access_token"].is_string())
    {
        outErrorMsg = "Invalid token response from Google.";
        return false;
    }

    long long nowUtc = static_cast<long long>(std::time(nullptr));
    std::string accessToken = tok["access_token"].get<std::string>();
    std::string refreshToken = tok.value("refresh_token", std::string(""));
    std::string idToken = tok.value("id_token", std::string(""));
    long long expiresAt = nowUtc + tok.value("expires_in", 3600LL);

    std::string email = ExtractEmailFromIdToken(idToken);
    if (email.empty()) email = FetchEmailFromUserInfo(accessToken, pStopFlag);
    if (email.empty())
    {
        outErrorMsg = "Could not determine email address from Google account.";
        return false;
    }

    std::string rawBlob = BuildIdeRawBlob(accessToken, refreshToken, idToken, expiresAt);
    json newAcc = MakeDefaultAccountRecord(email, "", accessToken, refreshToken, idToken, expiresAt, rawBlob, true);
    FetchCloudCodeForAccount(newAcc, pStopFlag);
    SyncActiveTokenToIde(newAcc);

    ScopedVaultMutex lock;
    std::string curStr;
    json arr = json::array();
    if (ReadVaultJsonUnlocked(curStr))
    {
        json parsed = json::parse(curStr, nullptr, false);
        if (parsed.is_array()) arr = parsed;
    }

    int foundIdx = -1;
    for (size_t i = 0; i < arr.size(); ++i)
    {
        if (arr[i].is_object())
        {
            arr[i]["IsActive"] = false;
            std::string existingEmail = arr[i].value("Email", std::string(""));
            if (!existingEmail.empty() && _stricmp(existingEmail.c_str(), email.c_str()) == 0)
            {
                foundIdx = static_cast<int>(i);
            }
        }
    }

    if (foundIdx >= 0)
    {
        json& dst = arr[foundIdx];
        dst["IsActive"] = true;
        dst["AccessToken"] = newAcc["AccessToken"];
        if (!refreshToken.empty()) dst["RefreshToken"] = refreshToken;
        if (!idToken.empty()) dst["IdToken"] = idToken;
        dst["ExpiresAt"] = expiresAt;
        dst["RawBlob"] = rawBlob;
        dst["AuthFailed"] = false;
        dst["GFiveH"] = newAcc["GFiveH"];
        dst["GFiveHReset"] = newAcc["GFiveHReset"];
        dst["GWeekly"] = newAcc["GWeekly"];
        dst["GWeeklyReset"] = newAcc["GWeeklyReset"];
        dst["CFiveH"] = newAcc["CFiveH"];
        dst["CFiveHReset"] = newAcc["CFiveHReset"];
        dst["CWeekly"] = newAcc["CWeekly"];
        dst["CWeeklyReset"] = newAcc["CWeeklyReset"];
        dst["HasArgon"] = newAcc["HasArgon"];
        dst["AWeekly"] = newAcc["AWeekly"];
        dst["AWeeklyReset"] = newAcc["AWeeklyReset"];
    }
    else
    {
        arr.push_back(newAcc);
    }

    std::string finalStr = arr.dump();
    WriteVaultJsonInternal(finalStr);
    outUpdatedVaultJson = finalStr;
    return true;
}

} // namespace QuotaSync
