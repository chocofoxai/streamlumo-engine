// streamlumo-engine/src/platform/platform_linux.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// Linux-specific platform implementations

#ifdef __linux__

#include "platform.h"

#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <signal.h>
#include <fnmatch.h>
#include <spawn.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <linux/limits.h>
#include <termios.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>

extern char** environ;

namespace streamlumo {
namespace platform {

// =============================================================================
// System Information
// =============================================================================

std::string getOSName() {
    std::ifstream osRelease("/etc/os-release");
    if (osRelease) {
        std::string line;
        while (std::getline(osRelease, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                std::string name = line.substr(12);
                // Remove quotes
                if (name.front() == '"') name = name.substr(1);
                if (name.back() == '"') name.pop_back();
                return name;
            }
        }
    }
    
    // Fallback
    struct utsname uts;
    if (uname(&uts) == 0) {
        return std::string(uts.sysname) + " " + uts.release;
    }
    
    return "Linux";
}

std::string getOSVersion() {
    struct utsname uts;
    if (uname(&uts) == 0) {
        return std::string(uts.release);
    }
    return "Unknown";
}

std::string getCPUName() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") == 0) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string name = line.substr(pos + 1);
                    // Trim
                    size_t start = name.find_first_not_of(' ');
                    if (start != std::string::npos) {
                        return name.substr(start);
                    }
                }
            }
        }
    }
    return "Unknown";
}

uint64_t getTotalMemoryBytes() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return static_cast<uint64_t>(info.totalram) * info.mem_unit;
    }
    return 0;
}

uint64_t getAvailableMemoryBytes() {
    // Try /proc/meminfo for more accurate available memory
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo) {
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("MemAvailable:") == 0) {
                uint64_t kb = 0;
                sscanf(line.c_str(), "MemAvailable: %lu", &kb);
                return kb * 1024;
            }
        }
    }
    
    // Fallback to sysinfo
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return static_cast<uint64_t>(info.freeram) * info.mem_unit;
    }
    return 0;
}

int getCPUCoreCount() {
    // Get physical core count from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        std::set<std::pair<int, int>> coreIds;
        int physicalId = -1, coreId = -1;
        std::string line;
        
        while (std::getline(cpuinfo, line)) {
            if (line.find("physical id") == 0) {
                sscanf(line.c_str(), "physical id : %d", &physicalId);
            } else if (line.find("core id") == 0) {
                sscanf(line.c_str(), "core id : %d", &coreId);
                if (physicalId >= 0 && coreId >= 0) {
                    coreIds.insert({physicalId, coreId});
                    physicalId = coreId = -1;
                }
            }
        }
        
        if (!coreIds.empty()) {
            return coreIds.size();
        }
    }
    
    // Fallback
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores > 0 ? static_cast<int>(cores) : 1;
}

int getCPUThreadCount() {
    long threads = sysconf(_SC_NPROCESSORS_ONLN);
    return threads > 0 ? static_cast<int>(threads) : 1;
}

uint32_t getCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

uint64_t getCurrentThreadId() {
    return static_cast<uint64_t>(pthread_self());
}

// =============================================================================
// Path Utilities
// =============================================================================

std::string getExecutablePath() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    
    if (len != -1) {
        path[len] = '\0';
        return std::string(path);
    }
    
    return "";
}

std::string getExecutableDir() {
    std::string path = getExecutablePath();
    return getDirectory(path);
}

std::string getHomeDir() {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home);
    }
    
    // Fallback to /tmp
    return "/tmp";
}

std::string getAppDataDir() {
    const char* xdgConfig = getenv("XDG_CONFIG_HOME");
    if (xdgConfig) {
        return joinPath(xdgConfig, "streamlumo");
    }
    return joinPath(getHomeDir(), ".config/streamlumo");
}

std::string getCacheDir() {
    const char* xdgCache = getenv("XDG_CACHE_HOME");
    if (xdgCache) {
        return joinPath(xdgCache, "streamlumo");
    }
    return joinPath(getHomeDir(), ".cache/streamlumo");
}

std::string getTempDir() {
    const char* tmp = getenv("TMPDIR");
    if (tmp) {
        return std::string(tmp);
    }
    
    tmp = getenv("TMP");
    if (tmp) {
        return std::string(tmp);
    }
    
    return "/tmp";
}

bool pathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool isFile(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

bool createDirectory(const std::string& path) {
    if (path.empty()) return false;
    if (pathExists(path)) return isDirectory(path);
    
    // Create parent directories
    std::string parent = getDirectory(path);
    if (!parent.empty() && parent != path && parent != "/") {
        if (!createDirectory(parent)) {
            return false;
        }
    }
    
    return mkdir(path.c_str(), 0755) == 0;
}

std::vector<std::string> listDirectory(const std::string& path, 
                                        const std::string& pattern) {
    std::vector<std::string> result;
    
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return result;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        
        // Skip . and ..
        if (name == "." || name == "..") {
            continue;
        }
        
        // Apply pattern filter if specified
        if (!pattern.empty()) {
            if (fnmatch(pattern.c_str(), name.c_str(), 0) != 0) {
                continue;
            }
        }
        
        result.push_back(name);
    }
    
    closedir(dir);
    return result;
}

// =============================================================================
// Environment Variables
// =============================================================================

std::string getEnv(const std::string& name, const std::string& defaultValue) {
    const char* value = getenv(name.c_str());
    return value ? std::string(value) : defaultValue;
}

bool setEnv(const std::string& name, const std::string& value) {
    return setenv(name.c_str(), value.c_str(), 1) == 0;
}

bool unsetEnv(const std::string& name) {
    return unsetenv(name.c_str()) == 0;
}

// =============================================================================
// Dynamic Library Loading
// =============================================================================

LibraryHandle loadLibrary(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void unloadLibrary(LibraryHandle handle) {
    if (handle) {
        dlclose(handle);
    }
}

void* getLibrarySymbol(LibraryHandle handle, const std::string& name) {
    if (!handle) return nullptr;
    return dlsym(handle, name.c_str());
}

std::string getLibraryError() {
    const char* error = dlerror();
    return error ? std::string(error) : "";
}

// =============================================================================
// Process Management
// =============================================================================

int executeCommand(const std::string& command, 
                   std::string* output,
                   std::string* errorOutput) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return -1;
    }
    
    if (output) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            *output += buffer;
        }
    }
    
    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

uint32_t startProcess(const std::string& command) {
    pid_t pid;
    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    
    int result = posix_spawn(&pid, "/bin/sh", &actions, nullptr, 
                              const_cast<char**>(argv), environ);
    
    posix_spawn_file_actions_destroy(&actions);
    
    if (result != 0) {
        return 0;
    }
    
    return static_cast<uint32_t>(pid);
}

bool isProcessRunning(uint32_t pid) {
    return kill(static_cast<pid_t>(pid), 0) == 0;
}

bool terminateProcess(uint32_t pid) {
    return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

// =============================================================================
// Threading Utilities
// =============================================================================

void setThreadName(const std::string& name) {
    prctl(PR_SET_NAME, name.substr(0, 15).c_str(), 0, 0, 0);
}

std::string getThreadName() {
    char name[64] = {0};
    prctl(PR_GET_NAME, name, 0, 0, 0);
    return std::string(name);
}

bool setThreadPriority(ThreadPriority priority) {
    int policy;
    struct sched_param param;
    pthread_getschedparam(pthread_self(), &policy, &param);
    
    int minPrio = sched_get_priority_min(policy);
    int maxPrio = sched_get_priority_max(policy);
    int range = maxPrio - minPrio;
    
    switch (priority) {
        case ThreadPriority::Lowest:
            param.sched_priority = minPrio;
            break;
        case ThreadPriority::BelowNormal:
            param.sched_priority = minPrio + range / 4;
            break;
        case ThreadPriority::Normal:
            param.sched_priority = minPrio + range / 2;
            break;
        case ThreadPriority::AboveNormal:
            param.sched_priority = minPrio + (range * 3) / 4;
            break;
        case ThreadPriority::Highest:
        case ThreadPriority::TimeCritical:
            param.sched_priority = maxPrio;
            break;
    }
    
    return pthread_setschedparam(pthread_self(), policy, &param) == 0;
}

bool setThreadAffinity(uint64_t mask) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int i = 0; i < 64; i++) {
        if (mask & (1ULL << i)) {
            CPU_SET(i, &cpuset);
        }
    }
    
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
}

// =============================================================================
// Memory Utilities
// =============================================================================

void* alignedAlloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

void alignedFree(void* ptr) {
    free(ptr);
}

bool lockMemory(void* ptr, size_t size) {
    return mlock(ptr, size) == 0;
}

bool unlockMemory(void* ptr, size_t size) {
    return munlock(ptr, size) == 0;
}

// =============================================================================
// High-Resolution Timing
// =============================================================================

uint64_t getTimestampNanos() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

// =============================================================================
// Console Utilities
// =============================================================================

void enableConsoleColors() {
    // Colors are enabled by default on Linux terminals
}

bool isTerminal() {
    return isatty(STDOUT_FILENO) != 0;
}

int getTerminalWidth() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80;
}

// =============================================================================
// Signal Handling
// =============================================================================

static std::function<void(int)> s_signalHandlers[32];

static void internalSignalHandler(int sig) {
    if (sig >= 0 && sig < 32 && s_signalHandlers[sig]) {
        s_signalHandlers[sig](sig);
    }
}

void installSignalHandler(int signal, SignalHandler handler) {
    if (signal >= 0 && signal < 32) {
        s_signalHandlers[signal] = handler;
        struct sigaction sa;
        sa.sa_handler = internalSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(signal, &sa, nullptr);
    }
}

void installCrashHandlers(const std::string& crashLogPath) {
    auto crashHandler = [crashLogPath](int sig) {
        std::ofstream log(crashLogPath, std::ios::app);
        if (log) {
            time_t now = time(nullptr);
            log << "=== CRASH at " << ctime(&now);
            log << "Signal: " << sig << " (";
            switch (sig) {
                case SIGSEGV: log << "SIGSEGV"; break;
                case SIGBUS: log << "SIGBUS"; break;
                case SIGFPE: log << "SIGFPE"; break;
                case SIGILL: log << "SIGILL"; break;
                case SIGABRT: log << "SIGABRT"; break;
                default: log << "Unknown"; break;
            }
            log << ")\n" << std::endl;
        }
        
        // Re-raise to get default behavior
        signal(sig, SIG_DFL);
        raise(sig);
    };
    
    installSignalHandler(SIGSEGV, crashHandler);
    installSignalHandler(SIGBUS, crashHandler);
    installSignalHandler(SIGFPE, crashHandler);
    installSignalHandler(SIGILL, crashHandler);
    installSignalHandler(SIGABRT, crashHandler);
}

} // namespace platform
} // namespace streamlumo

#endif // __linux__
