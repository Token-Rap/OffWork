#pragma once

#include "update_info.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace offwork {

inline constexpr UINT kUpdateCompleteMessage = WM_APP + 20;
inline constexpr UINT kUpdateProgressMessage = WM_APP + 21;

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

using UpdateProgress = std::function<void(int)>;
// A missing release requests a check; a supplied release requests a download.
using UpdateWork = std::function<UpdateResult(const std::optional<ReleaseInfo>&,
    const std::atomic_bool&, const UpdateProgress&)>;

struct UpdateTask;

class Updater {
public:
    explicit Updater(UpdateWork work = {});
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    Updater(Updater&&) = delete;
    Updater& operator=(Updater&&) = delete;
    void Check(HWND window);
    void Download();
    // UI-thread handlers for pointer-free WM_APP notifications. Stale messages
    // from earlier operations are ignored.
    void Complete(WPARAM operation);
    void Progress(WPARAM operation, LPARAM percent);
    // Returns true only after the downloaded installer was launched.
    bool LaunchReadyInstaller();
    // Call from WM_DESTROY, while the HWND is still valid. Never waits for I/O.
    void Stop();
    bool Available() const { return release_.has_value(); }
    bool Downloading() const { return downloading_; }
    int DownloadPercent() const { return percent_; }
    unsigned ProgressPhase() const { return progressPhase_; }
private:
    void Start(bool download);
    UpdateWork work_;
    std::shared_ptr<UpdateTask> task_;
    std::optional<ReleaseInfo> release_;
    std::unique_ptr<DownloadedInstaller> installer_;
    HWND window_ = nullptr;
    WPARAM operation_ = 0;
    ULONGLONG nextCheck_ = 0;
    int percent_ = -1;
    unsigned progressPhase_ = 0;
    bool downloading_ = false;
    bool stopped_ = false;
};

}  // namespace offwork
