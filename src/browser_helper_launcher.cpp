#include "browser_helper_launcher.h"
#include "logging.h"

#ifdef __APPLE__
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libgen.h>
#include <signal.h>
#include <mach-o/dyld.h>
extern char **environ;
#endif

#include <filesystem>

namespace fs = std::filesystem;

namespace streamlumo {

BrowserHelperLauncher::BrowserHelperLauncher() = default;
BrowserHelperLauncher::~BrowserHelperLauncher()
{
    stop();
}

bool BrowserHelperLauncher::isRunning() const
{
    return m_childPid > 0;
}

bool BrowserHelperLauncher::checkAlive()
{
#ifdef __APPLE__
    if (m_childPid <= 0) {
        return false;
    }
    if (kill(m_childPid, 0) == 0) {
        return true;
    }
    int status = 0;
    waitpid(m_childPid, &status, WNOHANG);
    m_childPid = -1;
    return false;
#else
    return false;
#endif
}

void BrowserHelperLauncher::stop()
{
#ifdef __APPLE__
    if (m_childPid > 0) {
        // First send SIGTERM for graceful shutdown
        log_info("[helper] sending SIGTERM to browser helper pid=%d", m_childPid);
        kill(m_childPid, SIGTERM);
        
        // Wait up to 3 seconds for graceful shutdown
        int status = 0;
        for (int i = 0; i < 30; ++i) {
            int result = waitpid(m_childPid, &status, WNOHANG);
            if (result > 0) {
                // Process exited
                log_info("[helper] browser helper exited gracefully pid=%d status=%d", m_childPid, WEXITSTATUS(status));
                m_childPid = -1;
                return;
            } else if (result < 0) {
                // Error or process doesn't exist
                log_info("[helper] waitpid returned error, assuming process already exited");
                m_childPid = -1;
                return;
            }
            // Still running, wait a bit
            usleep(100000); // 100ms
        }
        
        // Process didn't exit gracefully, force kill
        log_warn("[helper] browser helper did not exit gracefully, sending SIGKILL");
        kill(m_childPid, SIGKILL);
        waitpid(m_childPid, &status, 0);
        log_info("[helper] browser helper force killed pid=%d", m_childPid);
        m_childPid = -1;
    }
#endif
}

bool BrowserHelperLauncher::start(const std::string &helperBundlePath)
{
    if (isRunning()) {
        return true;
    }

#ifdef __APPLE__
    std::string binaryPath = resolveBinaryPath(helperBundlePath);
    if (binaryPath.empty()) {
        log_warn("[helper] helper bundle not found: %s", helperBundlePath.c_str());
        return false;
    }

    if (!launchProcess(binaryPath)) {
        log_warn("[helper] failed to launch helper: %s", binaryPath.c_str());
        return false;
    }

    log_info("[helper] launched browser helper: %s (pid=%d)", binaryPath.c_str(), m_childPid);
    return true;
#else
    (void)helperBundlePath;
    return false;
#endif
}

#ifdef __APPLE__
std::string BrowserHelperLauncher::resolveBinaryPath(const std::string &helperBundlePath) const
{
    fs::path bundle(helperBundlePath);
    if (!fs::exists(bundle)) {
        return {};
    }
    fs::path binary = bundle / "Contents" / "MacOS" / "streamlumo-browser-helper";
    if (fs::exists(binary)) {
        return binary.string();
    }
    return {};
}

bool BrowserHelperLauncher::launchProcess(const std::string &binaryPath)
{
    pid_t pid = 0;
    const char *argv[] = {binaryPath.c_str(), nullptr};
    int rc = posix_spawn(&pid, binaryPath.c_str(), nullptr, nullptr, const_cast<char **>(argv), environ);
    if (rc != 0) {
        log_warn("[helper] posix_spawn failed (%d) for %s", rc, binaryPath.c_str());
        return false;
    }
    m_childPid = pid;
    return true;
}
#endif

} // namespace streamlumo
