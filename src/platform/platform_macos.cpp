// streamlumo-engine/src/platform/platform_macos.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// macOS-specific platform implementations

#ifdef __APPLE__

#include "platform.h"

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <signal.h>
#include <fnmatch.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <regex>

extern char** environ;

namespace streamlumo {
namespace platform {

// =============================================================================
// System Information
// =============================================================================

std::string getOSName() {
    return "macOS";
}

std::string getOSVersion() {
    char version[256] = {0};
    size_t size = sizeof(version);
    
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0) {
        return std::string(version);
    }
    
    // Fallback to kern.osrelease
    if (sysctlbyname("kern.osrelease", version, &size, nullptr, 0) == 0) {
        return std::string(version);
    }
    
    return "Unknown";
}

std::string getCPUName() {
    char name[256] = {0};
    size_t size = sizeof(name);
    
    if (sysctlbyname("machdep.cpu.brand_string", name, &size, nullptr, 0) == 0) {
        return std::string(name);
    }
    
    return "Unknown";
}

uint64_t getTotalMemoryBytes() {
    int64_t mem = 0;
    size_t size = sizeof(mem);
    
    if (sysctlbyname("hw.memsize", &mem, &size, nullptr, 0) == 0) {
        return static_cast<uint64_t>(mem);
    }
    
    return 0;
}

uint64_t getAvailableMemoryBytes() {
    mach_port_t host = mach_host_self();
    vm_statistics64_data_t vmstat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    
    if (host_statistics64(host, HOST_VM_INFO64, 
                          reinterpret_cast<host_info64_t>(&vmstat), &count) == KERN_SUCCESS) {
        vm_size_t pageSize;
        host_page_size(host, &pageSize);
        return static_cast<uint64_t>(vmstat.free_count) * pageSize;
    }
    
    return 0;
}

int getCPUCoreCount() {
    int cores = 0;
    size_t size = sizeof(cores);
    
    if (sysctlbyname("hw.physicalcpu", &cores, &size, nullptr, 0) == 0) {
        return cores;
    }
    
    return 1;
}

int getCPUThreadCount() {
    int threads = 0;
    size_t size = sizeof(threads);
    
    if (sysctlbyname("hw.logicalcpu", &threads, &size, nullptr, 0) == 0) {
        return threads;
    }
    
    return getCPUCoreCount();
}

uint32_t getCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

uint64_t getCurrentThreadId() {
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
}

// =============================================================================
// Path Utilities
// =============================================================================

std::string getExecutablePath() {
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    
    if (_NSGetExecutablePath(path, &size) == 0) {
        char resolved[PATH_MAX];
        if (realpath(path, resolved)) {
            return std::string(resolved);
        }
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
    
    return "/tmp";
}

std::string getAppDataDir() {
    return joinPath(getHomeDir(), "Library/Application Support/StreamLumo");
}

std::string getCacheDir() {
    return joinPath(getHomeDir(), "Library/Caches/StreamLumo");
}

std::string getTempDir() {
    const char* tmp = getenv("TMPDIR");
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
    if (!parent.empty() && parent != path) {
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
    std::string cmd = command;
    
    FILE* pipe = popen(cmd.c_str(), "r");
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
    pthread_setname_np(name.substr(0, 15).c_str());
}

std::string getThreadName() {
    char name[64] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
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
    // macOS doesn't support thread affinity in the traditional sense
    // Use thread_policy_set for quality of service hints instead
    thread_affinity_policy_data_t policy = {static_cast<integer_t>(mask)};
    return thread_policy_set(mach_thread_self(), 
                             THREAD_AFFINITY_POLICY,
                             reinterpret_cast<thread_policy_t>(&policy),
                             THREAD_AFFINITY_POLICY_COUNT) == KERN_SUCCESS;
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

static mach_timebase_info_data_t s_timebaseInfo = {0, 0};

uint64_t getTimestampNanos() {
    if (s_timebaseInfo.denom == 0) {
        mach_timebase_info(&s_timebaseInfo);
    }
    
    uint64_t machTime = mach_absolute_time();
    return machTime * s_timebaseInfo.numer / s_timebaseInfo.denom;
}

// =============================================================================
// Console Utilities
// =============================================================================

void enableConsoleColors() {
    // Colors are enabled by default on macOS terminal
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
    // Install handlers for common crash signals
    auto crashHandler = [crashLogPath](int sig) {
        // Write crash info to log file
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

#endif // __APPLE__
