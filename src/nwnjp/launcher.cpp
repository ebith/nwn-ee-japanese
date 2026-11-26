// Starts nwmain.exe suspended, injects nwnjp.dll, then resumes.
//
// Usage: nwnjp_loader.exe [game directory | path to nwmain.exe]
// Without an argument the game is expected next to this launcher; an argument may name
// either the game directory or the executable itself. nwnjp.dll is always taken from
// this launcher's directory. The working directory becomes the game's own directory,
// which is where the game expects to start.
//
// The patch sites in nwnjp.dll are pinned to one executable, so a game update leaves the
// installed Japanese dialog.tlk in place while the patches that make it readable no longer
// apply. That state is not worth starting: refuse the mismatched build, and when the
// original table is still next to the game, offer to put it back and start in English.

#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr const char *kDllName = "nwnjp.dll";
constexpr const char *kExeName = "nwmain.exe";

// The game build the patch sites in nwnjp.dll were verified against.
constexpr const char *kSupportedVersion = "v89.8193.37-17";
constexpr const char *kSupportedExeSha256 =
    "3b7cb1252e0edb2ce22d7971f333aade027039ae30a45b4bc64732c3e6bec73a";

constexpr const char *kTlkBackupName = "dialog.tlk.nwnjp-backup";

std::string DirectoryOf(const std::string &path) {
    const size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string OwnDirectory() {
    char path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) return {};
    return DirectoryOf(path);
}

std::string Narrow(const wchar_t *text) {
    char buffer[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_ACP, 0, text, -1, buffer, sizeof(buffer), nullptr, nullptr);
    return buffer;
}

// The build version and the backup name are plain ASCII, so widening them here keeps the
// dialog text and the constants in one place.
std::wstring Widen(const std::string &text) {
    std::wstring wide;
    wide.reserve(text.size());
    for (char ch : text) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    return wide;
}

std::wstring VersionMismatchMessage() {
    return L"未対応のゲームバージョンです。\n"
           L"このままでは日本語が化けます。\n"
           L"\n"
           L"対応版: " +
           Widen(kSupportedVersion) +
           L"\n"
           L"\n"
           L"「はい」で日本語化を解除して英語で起動します。\n"
           L"「いいえ」で中止します。";
}

std::wstring NoBackupMessage() {
    return L"未対応のゲームバージョンです。\n"
           L"このままでは日本語が化けます。\n"
           L"\n"
           L"対応版: " +
           Widen(kSupportedVersion) +
           L"\n"
           L"\n"
           L"日本語化を解除するためのバックアップが見つかりません:\n"
           L"lang\\en\\data\\" + Widen(kTlkBackupName) +
           L"\n"
           L"\n"
           L"対応版のリリースを入れるか、インストーラで入れ直してください。";
}

bool IsDirectory(const std::string &path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileExists(const std::string &path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Read a file whole and return its SHA-256 as lowercase hex. The game executable is the
// only file large enough for the streaming to matter, and 1 MB chunks keep the buffer
// off the stack.
bool Sha256OfFile(const std::string &path, std::string &hex) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE;
    if (ok) ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    static BYTE chunk[1 << 20];
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, chunk, sizeof(chunk), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        if (!CryptHashData(hash, chunk, read, 0)) {
            ok = false;
            break;
        }
    }
    BYTE digest[32]{};
    DWORD digestSize = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0) != FALSE;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    if (!ok) return false;
    static const char digits[] = "0123456789abcdef";
    hex.clear();
    for (DWORD index = 0; index < digestSize; ++index) {
        hex += digits[digest[index] >> 4];
        hex += digits[digest[index] & 0x0F];
    }
    return true;
}

// The game directory holds lang\en\data\dialog.tlk; walk up from bin\win32 instead of
// assuming how many levels the executable sits below it.
std::string FindDataDirectory(const std::string &exeDirectory) {
    std::string directory = exeDirectory;
    for (int level = 0; level < 4 && directory.size() > 3; ++level) {
        const std::string candidate = directory + "lang\\en\\data\\";
        if (FileExists(candidate + "dialog.tlk")) return candidate;
        const size_t slash = directory.find_last_of('\\', directory.size() - 2);
        if (slash == std::string::npos) break;
        directory = directory.substr(0, slash + 1);
    }
    return {};
}

// Putting the original table back is all the uninstall needs: the game falls back to
// dialog.tlk when dialogf.tlk is absent, and the backup of the female table cannot be
// relied on here (an older install had already replaced it with the Japanese one).
bool RestoreEnglishTlk(const std::string &dataDirectory) {
    const std::string backup = dataDirectory + kTlkBackupName;
    if (!FileExists(backup)) return false;
    if (!CopyFileA(backup.c_str(), (dataDirectory + "dialog.tlk").c_str(), FALSE)) return false;
    DeleteFileA((dataDirectory + "dialogf.tlk").c_str());
    return true;
}

// The dialog only exists to keep a mismatched game from running with a table the loaded
// stubs cannot render, so it is not worth repeating once the original table is back.
bool SameFile(const std::string &left, const std::string &right) {
    std::string leftHash;
    std::string rightHash;
    if (!Sha256OfFile(left, leftHash)) return false;
    if (!Sha256OfFile(right, rightHash)) return false;
    return leftHash == rightHash;
}

bool InjectDll(HANDLE process, const std::string &dllPath) {
    const SIZE_T bytes = dllPath.size() + 1;
    void *remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::printf("VirtualAllocEx failed: %lu\n", GetLastError());
        return false;
    }
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        std::printf("WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel32, "LoadLibraryA"));
    if (!loadLibrary) {
        std::printf("LoadLibraryA not found\n");
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        std::printf("CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    WaitForSingleObject(thread, 15000);
    DWORD moduleResult = 0;
    GetExitCodeThread(thread, &moduleResult);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (moduleResult == 0) {
        std::printf("LoadLibraryA in the target returned NULL\n");
        return false;
    }
    return true;
}

int RunGame(const std::string &exePath, const std::string &gameDir, const std::string &dllPath,
            bool inject) {
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::string commandLine = "\"" + exePath + "\"";
    if (!CreateProcessA(exePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, gameDir.c_str(), &startup, &process)) {
        std::printf("CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    if (inject && !InjectDll(process.hProcess, dllPath)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    std::printf("launched, pid %lu\n", process.dwProcessId);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    std::printf("game exited with 0x%08lX\n", exitCode);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
    const std::string launcherDir = OwnDirectory();
    const std::string dllPath = launcherDir + kDllName;

    std::string exePath;
    if (argc > 1) {
        std::string argument = Narrow(argv[1]);
        while (!argument.empty() && argument.back() == '\\') argument.pop_back();
        exePath = IsDirectory(argument) ? argument + "\\" + kExeName : argument;
    } else {
        exePath = launcherDir + kExeName;
    }
    const std::string gameDir = DirectoryOf(exePath);

    std::printf("game: %s\n", exePath.c_str());
    std::printf("dll : %s\n", dllPath.c_str());
    if (!FileExists(exePath)) {
        std::printf("game executable not found (pass its directory or path as the argument)\n");
        return 1;
    }
    if (!FileExists(dllPath)) {
        std::printf("%s not found next to this launcher\n", kDllName);
        return 1;
    }

    std::string hash;
    if (!Sha256OfFile(exePath, hash)) {
        std::printf("could not read the executable, starting without the version check\n");
    } else if (hash != kSupportedExeSha256) {
        std::printf("game sha256: %s\n", hash.c_str());
        std::printf("supported  : %s (%s)\n", kSupportedExeSha256, kSupportedVersion);
        const std::string dataDirectory = FindDataDirectory(gameDir);
        if (dataDirectory.empty() || !FileExists(dataDirectory + kTlkBackupName)) {
            const std::wstring message = NoBackupMessage();
            MessageBoxW(nullptr, message.c_str(), L"nwnjp", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (SameFile(dataDirectory + "dialog.tlk", dataDirectory + kTlkBackupName)) {
            std::printf("the English dialog.tlk is already back; starting without injection\n");
            return RunGame(exePath, gameDir, dllPath, false);
        }
        const std::wstring message = VersionMismatchMessage();
        if (MessageBoxW(nullptr, message.c_str(), L"nwnjp",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return 1;
        }
        if (!RestoreEnglishTlk(dataDirectory)) {
            std::printf("could not restore the English dialog.tlk\n");
            return 1;
        }
        std::printf("restored the English dialog.tlk; starting without injection\n");
        return RunGame(exePath, gameDir, dllPath, false);
    } else {
        std::printf("game sha256: %s (supported)\n", hash.c_str());
    }

    return RunGame(exePath, gameDir, dllPath, true);
}
