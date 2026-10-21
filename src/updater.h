#pragma once

#include "update_info.h"

#include <windows.h>
#include <atomic>
#include <future>
#include <memory>
#include <string>

namespace offwork {

// Unique temporary directory: incomplete downloads are deleted, and no shared
// predictable executable in TEMP is ever overwritten or executed.
struct DownloadedInstaller {
    std::wstring directory;
    std::wstring path;
    bool launched = false;
    ~DownloadedInstaller();
};

struct UpdateResult {
    std::optional<ReleaseInfo> release;
    std::unique_ptr<DownloadedInstaller> installer;
};

class Updater {
public:
    ~Updater();
    void Check();
    void Download();
    // Returns true only after the downloaded installer was launched.
    bool Poll();
    bool Available() const { return release_.has_value(); }
    bool Downloading() const { return downloading_; }
private:
    void Start(bool download);
    std::shared_ptr<std::atomic_bool> cancelled_ = std::make_shared<std::atomic_bool>(false);
    std::future<UpdateResult> task_;
    std::optional<ReleaseInfo> release_;
    ULONGLONG nextCheck_ = 0;
    bool downloading_ = false;
};

}  // namespace offwork
