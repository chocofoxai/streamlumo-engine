// streamlumo-engine/src/platform/platform.h
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// Platform abstraction layer for cross-platform compatibility

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>

namespace streamlumo {
namespace platform {

// =============================================================================
// Platform Detection
// =============================================================================

enum class OS {
    Unknown,
    Windows,
    macOS,
    Linux
};

enum class Architecture {
    Unknown,
    x86,
    x64,
    ARM64
};

#if defined(_WIN32) || defined(_WIN64)
    #define STREAMLUMO_WINDOWS 1
    #define STREAMLUMO_PLATFORM_NAME "Windows"
    constexpr OS CurrentOS = OS::Windows;
    #if defined(_WIN64)
        constexpr Architecture CurrentArch = Architecture::x64;
    #elif defined(_M_ARM64)
        constexpr Architecture CurrentArch = Architecture::ARM64;
    #else
        constexpr Architecture CurrentArch = Architecture::x86;
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #define STREAMLUMO_MACOS 1
    #define STREAMLUMO_PLATFORM_NAME "macOS"
    constexpr OS CurrentOS = OS::macOS;
    #if defined(__arm64__) || defined(__aarch64__)
        constexpr Architecture CurrentArch = Architecture::ARM64;
    #else
        constexpr Architecture CurrentArch = Architecture::x64;
    #endif
#elif defined(__linux__)
    #define STREAMLUMO_LINUX 1
    #define STREAMLUMO_PLATFORM_NAME "Linux"
    constexpr OS CurrentOS = OS::Linux;
    #if defined(__x86_64__)
        constexpr Architecture CurrentArch = Architecture::x64;
    #elif defined(__aarch64__)
        constexpr Architecture CurrentArch = Architecture::ARM64;
    #elif defined(__i386__)
        constexpr Architecture CurrentArch = Architecture::x86;
    #else
        constexpr Architecture CurrentArch = Architecture::Unknown;
    #endif
#else
    #define STREAMLUMO_PLATFORM_NAME "Unknown"
    constexpr OS CurrentOS = OS::Unknown;
    constexpr Architecture CurrentArch = Architecture::Unknown;
#endif

// POSIX-like systems (macOS and Linux)
#if defined(STREAMLUMO_MACOS) || defined(STREAMLUMO_LINUX)
    #define STREAMLUMO_POSIX 1
#endif

// =============================================================================
// Path Separator
// =============================================================================

#ifdef STREAMLUMO_WINDOWS
    constexpr char PathSeparator = '\\';
    constexpr const char* PathSeparatorStr = "\\";
    constexpr const char* SharedLibExtension = ".dll";
    constexpr const char* ExecutableExtension = ".exe";
#else
    constexpr char PathSeparator = '/';
    constexpr const char* PathSeparatorStr = "/";
    #ifdef STREAMLUMO_MACOS
        constexpr const char* SharedLibExtension = ".dylib";
    #else
        constexpr const char* SharedLibExtension = ".so";
    #endif
    constexpr const char* ExecutableExtension = "";
#endif

// =============================================================================
// System Information
// =============================================================================

/**
 * @brief Get the operating system name and version
 */
std::string getOSName();
std::string getOSVersion();

/**
 * @brief Get system hardware information
 */
std::string getCPUName();
uint64_t getTotalMemoryBytes();
uint64_t getAvailableMemoryBytes();
int getCPUCoreCount();
int getCPUThreadCount();

/**
 * @brief Get the current process ID
 */
uint32_t getCurrentProcessId();

/**
 * @brief Get current thread ID
 */
uint64_t getCurrentThreadId();

// =============================================================================
// Path Utilities
// =============================================================================

/**
 * @brief Get the executable path (directory containing the executable)
 */
std::string getExecutablePath();

/**
 * @brief Get the executable directory
 */
std::string getExecutableDir();

/**
 * @brief Get the user's home directory
 */
std::string getHomeDir();

/**
 * @brief Get the application data directory
 * - Windows: %APPDATA%\StreamLumo
 * - macOS: ~/Library/Application Support/StreamLumo
 * - Linux: ~/.config/streamlumo
 */
std::string getAppDataDir();

/**
 * @brief Get the user cache directory
 * - Windows: %LOCALAPPDATA%\StreamLumo\cache
 * - macOS: ~/Library/Caches/StreamLumo
 * - Linux: ~/.cache/streamlumo
 */
std::string getCacheDir();

/**
 * @brief Get the temporary directory
 */
std::string getTempDir();

/**
 * @brief Join path components with the platform-specific separator
 */
std::string joinPath(const std::string& base, const std::string& path);
std::string joinPath(const std::vector<std::string>& components);

/**
 * @brief Normalize a path (convert separators, resolve . and ..)
 */
std::string normalizePath(const std::string& path);

/**
 * @brief Get the directory portion of a path
 */
std::string getDirectory(const std::string& path);

/**
 * @brief Get the filename portion of a path
 */
std::string getFilename(const std::string& path);

/**
 * @brief Get the file extension (including the dot)
 */
std::string getExtension(const std::string& path);

/**
 * @brief Check if a path exists
 */
bool pathExists(const std::string& path);

/**
 * @brief Check if a path is a directory
 */
bool isDirectory(const std::string& path);

/**
 * @brief Check if a path is a file
 */
bool isFile(const std::string& path);

/**
 * @brief Create a directory (and parent directories if needed)
 */
bool createDirectory(const std::string& path);

/**
 * @brief List files in a directory
 * @param pattern Optional glob pattern (e.g., "*.dll")
 */
std::vector<std::string> listDirectory(const std::string& path, 
                                        const std::string& pattern = "");

// =============================================================================
// Environment Variables
// =============================================================================

/**
 * @brief Get an environment variable
 * @param name Variable name
 * @param defaultValue Default value if not set
 */
std::string getEnv(const std::string& name, const std::string& defaultValue = "");

/**
 * @brief Set an environment variable
 */
bool setEnv(const std::string& name, const std::string& value);

/**
 * @brief Unset an environment variable
 */
bool unsetEnv(const std::string& name);

// =============================================================================
// Dynamic Library Loading
// =============================================================================

/**
 * @brief Handle to a dynamically loaded library
 */
using LibraryHandle = void*;

/**
 * @brief Load a dynamic library
 * @param path Path to the library
 * @return Handle to the library, or nullptr on failure
 */
LibraryHandle loadLibrary(const std::string& path);

/**
 * @brief Unload a dynamic library
 */
void unloadLibrary(LibraryHandle handle);

/**
 * @brief Get a symbol from a dynamic library
 * @param handle Library handle
 * @param name Symbol name
 * @return Pointer to the symbol, or nullptr if not found
 */
void* getLibrarySymbol(LibraryHandle handle, const std::string& name);

/**
 * @brief Get the last library loading error
 */
std::string getLibraryError();

// =============================================================================
// Process Management
// =============================================================================

/**
 * @brief Execute a command and wait for completion
 * @param command Command to execute
 * @param output Output from the command (stdout)
 * @param errorOutput Error output from the command (stderr)
 * @return Exit code
 */
int executeCommand(const std::string& command, 
                   std::string* output = nullptr,
                   std::string* errorOutput = nullptr);

/**
 * @brief Start a background process
 * @param command Command to execute
 * @return Process ID, or 0 on failure
 */
uint32_t startProcess(const std::string& command);

/**
 * @brief Check if a process is running
 */
bool isProcessRunning(uint32_t pid);

/**
 * @brief Terminate a process
 */
bool terminateProcess(uint32_t pid);

// =============================================================================
// Threading Utilities
// =============================================================================

/**
 * @brief Set the name of the current thread (for debugging)
 */
void setThreadName(const std::string& name);

/**
 * @brief Get the current thread name
 */
std::string getThreadName();

/**
 * @brief Set thread priority
 */
enum class ThreadPriority {
    Lowest,
    BelowNormal,
    Normal,
    AboveNormal,
    Highest,
    TimeCritical
};

bool setThreadPriority(ThreadPriority priority);

/**
 * @brief Set CPU affinity for the current thread
 */
bool setThreadAffinity(uint64_t mask);

// =============================================================================
// Memory Utilities
// =============================================================================

/**
 * @brief Allocate aligned memory
 * @param size Size in bytes
 * @param alignment Alignment (must be power of 2)
 */
void* alignedAlloc(size_t size, size_t alignment);

/**
 * @brief Free aligned memory
 */
void alignedFree(void* ptr);

/**
 * @brief Lock memory to prevent paging (for real-time audio/video)
 */
bool lockMemory(void* ptr, size_t size);

/**
 * @brief Unlock memory
 */
bool unlockMemory(void* ptr, size_t size);

// =============================================================================
// High-Resolution Timing
// =============================================================================

/**
 * @brief Get a high-resolution timestamp in nanoseconds
 */
uint64_t getTimestampNanos();

/**
 * @brief Get a high-resolution timestamp in microseconds
 */
uint64_t getTimestampMicros();

/**
 * @brief Get a high-resolution timestamp in milliseconds
 */
uint64_t getTimestampMillis();

/**
 * @brief Sleep for the specified duration
 */
void sleepMillis(uint32_t millis);
void sleepMicros(uint32_t micros);

/**
 * @brief Precise sleep with busy-wait for final accuracy
 * Use for sub-millisecond precision when needed
 */
void preciseSleep(uint64_t nanos);

// =============================================================================
// Console Utilities
// =============================================================================

/**
 * @brief Enable ANSI color codes on Windows console
 */
void enableConsoleColors();

/**
 * @brief Check if stdout is a terminal (TTY)
 */
bool isTerminal();

/**
 * @brief Get terminal width in columns
 */
int getTerminalWidth();

// =============================================================================
// Signal Handling
// =============================================================================

/**
 * @brief Signal handler callback type
 */
using SignalHandler = std::function<void(int)>;

/**
 * @brief Install a signal handler
 */
void installSignalHandler(int signal, SignalHandler handler);

/**
 * @brief Install default crash handlers
 */
void installCrashHandlers(const std::string& crashLogPath);

} // namespace platform
} // namespace streamlumo
