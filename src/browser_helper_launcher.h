#pragma once

#include <string>

namespace streamlumo {

class BrowserHelperLauncher {
public:
    BrowserHelperLauncher();
    ~BrowserHelperLauncher();

    bool start(const std::string &helperBundlePath);
    void stop();
    bool isRunning() const;
    bool checkAlive();

private:
    bool launchProcess(const std::string &binaryPath);
    std::string resolveBinaryPath(const std::string &helperBundlePath) const;

    int m_childPid{-1};
};

} // namespace streamlumo
