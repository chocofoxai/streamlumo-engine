// streamlumo-engine/src/platform/platform_windows.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// Windows-specific platform implementations

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "platform.h"

#include <windows.h>
#include <shlobj.h>
#include <psapi.h>
#include <dbghelp.h>
#include <intrin.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shlwapi.lib")

namespace streamlumo {
namespace platform {

// =============================================================================
// Helper Functions
// =============================================================================

static std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 
                                    static_cast<int>(wide.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 
                        static_cast<int>(wide.size()),
                        &result[0], size, nullptr, nullptr);
    return result;
}

static std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), 
                                    static_cast<int>(utf8.size()),
                                    nullptr, 0);
    if (size <= 0) return L"";
    
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), 
                        static_cast<int>(utf8.size()),
                        &result[0], size);
    return result;
}

// =============================================================================
// System Information
// =============================================================================

std::string getOSName() {
    return "Windows";
}

std::string getOSVersion() {
    OSVERSIONINFOEXW osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    
    // Use RtlGetVersion to get accurate version info
    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        RtlGetVersionPtr rtlGetVersion = 
            reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion) {
            rtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&osvi));
        }
    }
    
    std::ostringstream oss;
    oss << osvi.dwMajorVersion << "." << osvi.dwMinorVersion 
        << "." << osvi.dwBuildNumber;
    
    return oss.str();
}

std::string getCPUName() {
    int cpuInfo[4] = {0};
    char cpuName[49] = {0};
    
    __cpuid(cpuInfo, 0x80000000);
    unsigned int maxExtId = cpuInfo[0];
    
    if (maxExtId >= 0x80000004) {
        __cpuid(cpuInfo, 0x80000002);
        memcpy(cpuName, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        memcpy(cpuName + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        memcpy(cpuName + 32, cpuInfo, sizeof(cpuInfo));
    }
    
    // Trim leading/trailing spaces
    std::string result = cpuName;
    size_t start = result.find_first_not_of(' ');
    size_t end = result.find_last_not_of(' ');
    
    if (start == std::string::npos) return "Unknown";
    return result.substr(start, end - start + 1);
}

uint64_t getTotalMemoryBytes() {
    MEMORYSTATUSEX memInfo = {0};
    memInfo.dwLength = sizeof(memInfo);
    
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullTotalPhys;
    }
    
    return 0;
}

uint64_t getAvailableMemoryBytes() {
    MEMORYSTATUSEX memInfo = {0};
    memInfo.dwLength = sizeof(memInfo);
    
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullAvailPhys;
    }
    
    return 0;
}

int getCPUCoreCount() {
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    
    // Get physical core count
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return sysInfo.dwNumberOfProcessors;
    }
    
    std::vector<uint8_t> buffer(length);
    auto info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
    
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length)) {
        return sysInfo.dwNumberOfProcessors;
    }
    
    int cores = 0;
    DWORD offset = 0;
    while (offset < length) {
        auto current = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            buffer.data() + offset);
        if (current->Relationship == RelationProcessorCore) {
            cores++;
        }
        offset += current->Size;
    }
    
    return cores > 0 ? cores : sysInfo.dwNumberOfProcessors;
}

int getCPUThreadCount() {
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    return sysInfo.dwNumberOfProcessors;
}

uint32_t getCurrentProcessId() {
    return GetCurrentProcessId();
}

uint64_t getCurrentThreadId() {
    return GetCurrentThreadId();
}

// =============================================================================
// Path Utilities
// =============================================================================

std::string getExecutablePath() {
    wchar_t path[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    
    if (length > 0 && length < MAX_PATH) {
        return wideToUtf8(path);
    }
    
    return "";
}

std::string getExecutableDir() {
    std::string path = getExecutablePath();
    return getDirectory(path);
}

std::string getHomeDir() {
    wchar_t path[MAX_PATH] = {0};
    
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path))) {
        return wideToUtf8(path);
    }
    
    // Fallback to USERPROFILE
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        return userProfile;
    }
    
    return "C:\\Users\\Default";
}

std::string getAppDataDir() {
    wchar_t path[MAX_PATH] = {0};
    
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return joinPath(wideToUtf8(path), "StreamLumo");
    }
    
    return joinPath(getHomeDir(), "AppData\\Roaming\\StreamLumo");
}

std::string getCacheDir() {
    wchar_t path[MAX_PATH] = {0};
    
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return joinPath(wideToUtf8(path), "StreamLumo\\cache");
    }
    
    return joinPath(getHomeDir(), "AppData\\Local\\StreamLumo\\cache");
}

std::string getTempDir() {
    wchar_t path[MAX_PATH] = {0};
    DWORD length = GetTempPathW(MAX_PATH, path);
    
    if (length > 0 && length < MAX_PATH) {
        return wideToUtf8(path);
    }
    
    return "C:\\Temp";
}

bool pathExists(const std::string& path) {
    std::wstring wpath = utf8ToWide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::string& path) {
    std::wstring wpath = utf8ToWide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool isFile(const std::string& path) {
    std::wstring wpath = utf8ToWide(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool createDirectory(const std::string& path) {
    if (path.empty()) return false;
    if (pathExists(path)) return isDirectory(path);
    
    // Create parent directories
    std::string parent = getDirectory(path);
    if (!parent.empty() && parent != path) {
        if (!createDirectory(parent)) {
            return false;
        }
    }
    
    std::wstring wpath = utf8ToWide(path);
    return CreateDirectoryW(wpath.c_str(), nullptr) != 0;
}

std::vector<std::string> listDirectory(const std::string& path, 
                                        const std::string& pattern) {
    std::vector<std::string> result;
    
    std::string searchPath = joinPath(path, pattern.empty() ? "*" : pattern);
    std::wstring wsearchPath = utf8ToWide(searchPath);
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(wsearchPath.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }
    
    do {
        std::wstring name = findData.cFileName;
        
        // Skip . and ..
        if (name == L"." || name == L"..") {
            continue;
        }
        
        result.push_back(wideToUtf8(name));
    } while (FindNextFileW(hFind, &findData));
    
    FindClose(hFind);
    return result;
}

// =============================================================================
// Environment Variables
// =============================================================================

std::string getEnv(const std::string& name, const std::string& defaultValue) {
    std::wstring wname = utf8ToWide(name);
    
    DWORD size = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (size == 0) {
        return defaultValue;
    }
    
    std::wstring value(size, 0);
    GetEnvironmentVariableW(wname.c_str(), &value[0], size);
    value.resize(size - 1); // Remove null terminator
    
    return wideToUtf8(value);
}

bool setEnv(const std::string& name, const std::string& value) {
    std::wstring wname = utf8ToWide(name);
    std::wstring wvalue = utf8ToWide(value);
    return SetEnvironmentVariableW(wname.c_str(), wvalue.c_str()) != 0;
}

bool unsetEnv(const std::string& name) {
    std::wstring wname = utf8ToWide(name);
    return SetEnvironmentVariableW(wname.c_str(), nullptr) != 0;
}

// =============================================================================
// Dynamic Library Loading
// =============================================================================

LibraryHandle loadLibrary(const std::string& path) {
    std::wstring wpath = utf8ToWide(path);
    return LoadLibraryW(wpath.c_str());
}

void unloadLibrary(LibraryHandle handle) {
    if (handle) {
        FreeLibrary(static_cast<HMODULE>(handle));
    }
}

void* getLibrarySymbol(LibraryHandle handle, const std::string& name) {
    if (!handle) return nullptr;
    return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(handle), name.c_str()));
}

std::string getLibraryError() {
    DWORD error = GetLastError();
    if (error == 0) return "";
    
    LPWSTR messageBuffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   nullptr, error, 0, 
                   reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
    
    std::string result = wideToUtf8(messageBuffer);
    LocalFree(messageBuffer);
    
    return result;
}

// =============================================================================
// Process Management
// =============================================================================

int executeCommand(const std::string& command, 
                   std::string* output,
                   std::string* errorOutput) {
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    
    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    HANDLE hStdErrRead = nullptr, hStdErrWrite = nullptr;
    
    if (output) {
        CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0);
        SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
    }
    
    if (errorOutput) {
        CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0);
        SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);
    }
    
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = output ? hStdOutWrite : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = errorOutput ? hStdErrWrite : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    
    PROCESS_INFORMATION pi = {0};
    
    std::wstring wcmd = utf8ToWide("cmd.exe /c " + command);
    
    if (!CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (hStdOutRead) CloseHandle(hStdOutRead);
        if (hStdOutWrite) CloseHandle(hStdOutWrite);
        if (hStdErrRead) CloseHandle(hStdErrRead);
        if (hStdErrWrite) CloseHandle(hStdErrWrite);
        return -1;
    }
    
    if (hStdOutWrite) CloseHandle(hStdOutWrite);
    if (hStdErrWrite) CloseHandle(hStdErrWrite);
    
    // Read output
    auto readPipe = [](HANDLE pipe, std::string* str) {
        if (!pipe || !str) return;
        
        char buffer[4096];
        DWORD bytesRead;
        
        while (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            str->append(buffer, bytesRead);
        }
    };
    
    readPipe(hStdOutRead, output);
    readPipe(hStdErrRead, errorOutput);
    
    WaitForSingleObject(pi.hProcess, INFINITE);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hStdOutRead) CloseHandle(hStdOutRead);
    if (hStdErrRead) CloseHandle(hStdErrRead);
    
    return static_cast<int>(exitCode);
}

uint32_t startProcess(const std::string& command) {
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    
    PROCESS_INFORMATION pi = {0};
    
    std::wstring wcmd = utf8ToWide(command);
    
    if (!CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                        nullptr, nullptr, &si, &pi)) {
        return 0;
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return pi.dwProcessId;
}

bool isProcessRunning(uint32_t pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    
    DWORD exitCode;
    bool running = GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE;
    
    CloseHandle(hProcess);
    return running;
}

bool terminateProcess(uint32_t pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess) return false;
    
    bool result = TerminateProcess(hProcess, 1) != 0;
    CloseHandle(hProcess);
    
    return result;
}

// =============================================================================
// Threading Utilities
// =============================================================================

void setThreadName(const std::string& name) {
    std::wstring wname = utf8ToWide(name);
    SetThreadDescription(GetCurrentThread(), wname.c_str());
}

std::string getThreadName() {
    PWSTR wname = nullptr;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wname)) && wname) {
        std::string result = wideToUtf8(wname);
        LocalFree(wname);
        return result;
    }
    return "";
}

bool setThreadPriority(ThreadPriority priority) {
    int winPriority;
    
    switch (priority) {
        case ThreadPriority::Lowest:
            winPriority = THREAD_PRIORITY_LOWEST;
            break;
        case ThreadPriority::BelowNormal:
            winPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case ThreadPriority::Normal:
            winPriority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::AboveNormal:
            winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case ThreadPriority::Highest:
            winPriority = THREAD_PRIORITY_HIGHEST;
            break;
        case ThreadPriority::TimeCritical:
            winPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        default:
            winPriority = THREAD_PRIORITY_NORMAL;
    }
    
    return SetThreadPriority(GetCurrentThread(), winPriority) != 0;
}

bool setThreadAffinity(uint64_t mask) {
    return SetThreadAffinityMask(GetCurrentThread(), 
                                  static_cast<DWORD_PTR>(mask)) != 0;
}

// =============================================================================
// Memory Utilities
// =============================================================================

void* alignedAlloc(size_t size, size_t alignment) {
    return _aligned_malloc(size, alignment);
}

void alignedFree(void* ptr) {
    _aligned_free(ptr);
}

bool lockMemory(void* ptr, size_t size) {
    return VirtualLock(ptr, size) != 0;
}

bool unlockMemory(void* ptr, size_t size) {
    return VirtualUnlock(ptr, size) != 0;
}

// =============================================================================
// High-Resolution Timing
// =============================================================================

static LARGE_INTEGER s_qpcFrequency = {0};

uint64_t getTimestampNanos() {
    if (s_qpcFrequency.QuadPart == 0) {
        QueryPerformanceFrequency(&s_qpcFrequency);
    }
    
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    
    // Convert to nanoseconds
    return static_cast<uint64_t>(
        (counter.QuadPart * 1000000000LL) / s_qpcFrequency.QuadPart);
}

// =============================================================================
// Console Utilities
// =============================================================================

void enableConsoleColors() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}

bool isTerminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    return GetConsoleMode(hOut, &mode) != 0;
}

int getTerminalWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return 80;
}

// =============================================================================
// Signal Handling
// =============================================================================

static SignalHandler s_ctrlHandler;

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (s_ctrlHandler) {
        switch (ctrlType) {
            case CTRL_C_EVENT:
                s_ctrlHandler(2); // SIGINT equivalent
                return TRUE;
            case CTRL_BREAK_EVENT:
                s_ctrlHandler(3); // SIGQUIT equivalent
                return TRUE;
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                s_ctrlHandler(15); // SIGTERM equivalent
                return TRUE;
        }
    }
    return FALSE;
}

void installSignalHandler(int signal, SignalHandler handler) {
    s_ctrlHandler = handler;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
}

static LONG WINAPI UnhandledExceptionHandler(PEXCEPTION_POINTERS exInfo) {
    // Get crash log path from environment or use default
    std::string crashLogPath = getEnv("STREAMLUMO_CRASH_LOG", 
                                       joinPath(getAppDataDir(), "crash.log"));
    
    std::ofstream log(crashLogPath, std::ios::app);
    if (log) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        log << "=== CRASH at " << st.wYear << "-" << st.wMonth << "-" << st.wDay
            << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << " ===\n";
        log << "Exception Code: 0x" << std::hex << exInfo->ExceptionRecord->ExceptionCode 
            << std::dec << "\n";
        log << "Exception Address: " << exInfo->ExceptionRecord->ExceptionAddress << "\n";
        log << std::endl;
    }
    
    return EXCEPTION_CONTINUE_SEARCH;
}

void installCrashHandlers(const std::string& crashLogPath) {
    setEnv("STREAMLUMO_CRASH_LOG", crashLogPath);
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
}

} // namespace platform
} // namespace streamlumo

#endif // _WIN32
