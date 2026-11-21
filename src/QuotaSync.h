#pragma once

#include <atomic>
#include <string>

namespace QuotaSync
{
    // Returns %LOCALAPPDATA%\AntigravityQuota\vault.dat
    std::wstring ResolveVaultPath();

    // Reads & DPAPI-decrypts vault.dat under the shared vault mutex.
    // Returns true and populates outJson if readable & non-empty.
    bool ReadVaultJson(std::string& outJson);

    // Bootstraps vault.dat from Antigravity IDE local credentials
    // (~/.gemini/jetski-standalone-oauth-token or Windows Credential Manager "gemini:antigravity")
    // if vault.dat does not exist or has no accounts.
    // Returns true if an account was imported and outVaultJson is populated.
    bool TryBootstrapFromIde(std::string& outVaultJson);

    // Refreshes OAuth token if expiring/expired, fetches Cloud Code quota for the active account
    // in vault.dat (bootstrapping from IDE if needed), and persists updated tokens & quotas
    // atomically back into vault.dat.
    // Returns true if outUpdatedVaultJson was populated (even if AuthFailed was flagged).
    bool PollActiveAccountAndUpdateVault(std::string& outUpdatedVaultJson, std::atomic<bool>* pStopFlag = nullptr);

    // Runs interactive Google PKCE sign-in in default browser (loopback port 51121),
    // fetches initial quota, saves as active account in vault.dat, and returns updated vault JSON.
    bool SignInWithBrowser(std::string& outUpdatedVaultJson, std::string& outErrorMsg, std::atomic<bool>* pStopFlag = nullptr);

    // Utility helpers exposed for unit testing
    long long ParseIsoUtcEpoch(const std::string& iso);
    std::string FormatResetDuration(long long targetEpochUtc, long long nowEpochUtc);
    bool ApplyQuotaSummaryJson(const std::string& summaryJson,
                               double& g5h, std::string& g5hReset,
                               double& gWk, std::string& gWkReset,
                               double& c5h, std::string& c5hReset,
                               double& cWk, std::string& cWkReset,
                               bool& hasArgon, double& aWk, std::string& aWkReset,
                               long long nowEpochUtc = 0);
}
