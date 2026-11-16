// Starts nwmain.exe suspended, injects nwnjp.dll, then resumes.
//
// Usage: nwnjp_loader.exe [game directory | path to nwmain.exe]
// Without an argument the game is expected next to this launcher; an argument may name
// either the game directory or the executable itself. nwnjp.dll is always taken from
// this launcher's directory. The working directory becomes the game's own directory,
// which is where the game expects to start.

#include <windows.h>
#include <cstdio>
#include <string>

namespace {

constexpr const char *kDllName = "nwnjp.dll";
constexpr const char *kExeName = "nwmain.exe";

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

bool IsDirectory(const std::string &path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
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
    if (GetFileAttributesA(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("game executable not found (pass its directory or path as the argument)\n");
        return 1;
    }
    if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("%s not found next to this launcher\n", kDllName);
        return 1;
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::string commandLine = "\"" + exePath + "\"";
    if (!CreateProcessA(exePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, gameDir.c_str(), &startup, &process)) {
        std::printf("CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    if (!InjectDll(process.hProcess, dllPath)) {
        TerminateProcess(process.hProcess, 1);
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
