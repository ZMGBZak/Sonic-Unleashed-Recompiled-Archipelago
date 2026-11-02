#pragma once
// AP local updater helper. This header is compiled into the main game EXE.
// It runs in helper mode BEFORE game initialization when invoked with a private argument.
// No APWorld changes, user saves, or other game files are touched.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <system_error>
#pragma comment(lib, "shell32.lib")
#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace APSelfUpdate
{
    inline bool IsHexSha256(const std::string& s)
    {
        if (s.size() != 64) return false;
        for (char c : s)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) return false;
        return true;
    }

    inline std::string LowerAscii(std::string s)
    {
        for (auto& c : s) if (c >= 'A' && c <= 'F') c = char(c - 'A' + 'a');
        return s;
    }

    // Streaming SHA-256 via the Windows CNG provider; supports large executables.
    inline bool Sha256File(const std::filesystem::path& path, std::string& out)
    {
        out.clear();
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ULONG bytes = 0, objectSize = 0, hashSize = 0;
        std::vector<UCHAR> object;
        std::vector<UCHAR> digest;
        bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
        if (ok) ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytes, 0) >= 0;
        if (ok) ok = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &bytes, 0) >= 0;
        if (ok) ok = objectSize && hashSize == 32;
        if (ok)
        {
            object.resize(objectSize);
            digest.resize(hashSize);
            ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
        }
        std::array<char, 65536> buffer{};
        while (ok && input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = input.gcount();
            if (read > 0)
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(read), 0) >= 0;
        }
        if (input.bad()) ok = false;
        if (ok) ok = BCryptFinishHash(hash, digest.data(), hashSize, 0) >= 0;
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!ok) return false;
        constexpr char hex[] = "0123456789abcdef";
        for (UCHAR c : digest)
        {
            out.push_back(hex[(c >> 4) & 0xf]);
            out.push_back(hex[c & 0xf]);
        }
        return true;
    }

    inline std::wstring Quote(const std::wstring& s)
    {
        // Only paths generated locally (no user-supplied quote-containing paths).
        return L"\"" + s + L"\"";
    }

    inline std::filesystem::path CurrentExePath()
    {
        std::wstring buffer(32768, L'\0');
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!n || n >= buffer.size()) return {};
        buffer.resize(n);
        return std::filesystem::path(buffer);
    }

    inline void ReportHelperError(const wchar_t* message)
    {
        MessageBoxW(nullptr, message, L"Sonic Unleashed - Update failed", MB_OK | MB_ICONERROR);
    }

    // Entry-point interception. Only acts on our own private helper argument.
    // Command format: --ap-internal-apply "target" "staged" "hash" parentPID
    inline bool HandleHelperInvocation(int argc, char* argv[], int& exitCode)
    {
        if (argc < 2 || std::string(argv[1]) != "--ap-internal-apply") return false;
        exitCode = 1;
        if (argc != 6) { ReportHelperError(L"Invalid updater arguments."); return true; }
        int wideArgc = 0;
        LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
        if (!wideArgv || wideArgc != 6)
        {
            if (wideArgv) LocalFree(wideArgv);
            ReportHelperError(L"Could not read updater Unicode arguments.");
            return true;
        }
        const std::filesystem::path target(wideArgv[2]);
        const std::filesystem::path staged(wideArgv[3]);
        const std::wstring hashText(wideArgv[4]);
        const std::wstring parentText(wideArgv[5]);
        LocalFree(wideArgv);
        // Capture and validate the checksum before replacing any game file.
        if (hashText.size() != 64 ||
            !std::all_of(hashText.begin(), hashText.end(), [](wchar_t c) {
                return (c >= L'0' && c <= L'9') ||
                       (c >= L'a' && c <= L'f') ||
                       (c >= L'A' && c <= L'F');
            }))
        {
            ReportHelperError(L"Invalid expected SHA-256 checksum.");
            return true;
        }
        const std::string expectedHash = LowerAscii(
            std::string(hashText.begin(), hashText.end()));

        if (target.filename() != L"UnleashedRecomp.exe" ||
            staged != std::filesystem::path(target.wstring() + L".apupdate.new") ||
            target.parent_path().empty() || target == staged)
        {
            ReportHelperError(L"Updater refused an unexpected target path.");
            return true;
        }
        wchar_t* end = nullptr;
        const unsigned long pid = std::wcstoul(parentText.c_str(), &end, 10);
        if (!pid || !end || *end || pid > UINT32_MAX)
        {
            ReportHelperError(L"Invalid parent process ID.");
            return true;
        }
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (parent)
        {
            const auto waited = WaitForSingleObject(parent, 120000);
            CloseHandle(parent);
            if (waited != WAIT_OBJECT_0)
            {
                ReportHelperError(L"The game did not close. Update cancelled; nothing was replaced.");
                return true;
            }
        }
        else if (GetLastError() != ERROR_INVALID_PARAMETER)
        {
            ReportHelperError(L"Unable to safely wait for the game to exit.");
            return true;
        }

        std::string actualHash;
        if (!Sha256File(staged, actualHash) || LowerAscii(actualHash) != expectedHash)
        {
            ReportHelperError(L"Downloaded executable checksum does not match. Nothing was replaced.");
            return true;
        }

        const std::filesystem::path backup(target.wstring() + L".apupdate.backup");
        // This is the ONLY game file that the updater renames/replaces.
        if (!MoveFileExW(target.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            ReportHelperError(L"Could not back up the old executable. Nothing was replaced.");
            return true;
        }
        if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            ReportHelperError(L"Could not install the new executable. Backup restoration attempted.");
            return true;
        }

        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        std::wstring command = Quote(target.wstring());
        if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, target.parent_path().c_str(), &si, &pi))
        {
            // The new executable could not start: restore the previous binary.
            MoveFileExW(target.c_str(), staged.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            ReportHelperError(L"Updated game could not start. Previous executable restoration attempted.");
            return true;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        exitCode = 0;
        return true;
    }

    inline bool LaunchHelper(const std::filesystem::path& staged, const std::string& sha, std::string& error)
    {
        if (!IsHexSha256(sha)) { error = "Invalid expected SHA-256"; return false; }
        const auto current = CurrentExePath();
        if (current.empty() || current.filename() != L"UnleashedRecomp.exe")
        {
            error = "Could not identify UnleashedRecomp.exe";
            return false;
        }
        wchar_t tempDir[MAX_PATH + 1]{};
        const DWORD n = GetTempPathW(MAX_PATH, tempDir);
        if (!n || n > MAX_PATH) { error = "Cannot locate temporary folder"; return false; }
        const auto helper = std::filesystem::path(tempDir) /
            (L"SonicAPUpdater_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
            std::to_wstring(GetTickCount64()) + L".exe");
        if (!CopyFileW(current.c_str(), helper.c_str(), TRUE))
        {
            error = "Cannot create temporary updater helper";
            return false;
        }
        // Uses a copied game executable, NOT a separately distributed updater.
        const std::wstring arguments = Quote(helper.wstring()) + L" --ap-internal-apply " +
            Quote(current.wstring()) + L" " + Quote(staged.wstring()) + L" " +
            std::wstring(sha.begin(), sha.end()) + L" " + std::to_wstring(GetCurrentProcessId());
        std::wstring command = arguments;
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        const BOOL launched = CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, current.parent_path().c_str(), &si, &pi);
        if (!launched)
        {
            DeleteFileW(helper.c_str());
            error = "Could not launch updater helper";
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
}
#endif
