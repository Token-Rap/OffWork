#include "updater.h"
#include "version.h"

#include <winhttp.h>
#include <objbase.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace offwork {

// The worker owns this mailbox independently of Updater/AppState. The same
// short lock gates PostMessage and HWND invalidation; no I/O runs under it.
struct UpdateTask {
    std::mutex mutex;
    std::atomic_bool cancelled = false;
    HWND window = nullptr;
    WPARAM operation = 0;
    std::optional<UpdateResult> result;

    void Progress(int percent) {
        std::lock_guard lock(mutex);
        if (window && !cancelled) {
            PostMessageW(window, kUpdateProgressMessage, operation, percent);
        }
    }

    void Complete(UpdateResult completed) {
        std::lock_guard lock(mutex);
        if (window && !cancelled) {
            result = std::move(completed);
            PostMessageW(window, kUpdateCompleteMessage, operation, 0);
        }
    }
};

namespace {
constexpr ULONGLONG kCheckInterval = 6ull * 60 * 60 * 1000;
constexpr std::size_t kMaxInstallerBytes = 64 * 1024 * 1024;

struct InternetCloser { void operator()(void* p) const { if (p) WinHttpCloseHandle(p); } };
using Internet = std::unique_ptr<void, InternetCloser>;
struct FileCloser { void operator()(void* p) const { if (p != INVALID_HANDLE_VALUE) CloseHandle(p); } };
using File = std::unique_ptr<void, FileCloser>;

void CleanupStaleDownloads(const std::atomic_bool& cancelled) {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) return;
    std::filesystem::directory_iterator it(root, error), end;
    for (int examined = 0; !cancelled && !error && it != end && examined < 4096; it.increment(error), ++examined) {
        const auto path = it->path();
        const auto name = path.filename().wstring();
        constexpr std::wstring_view prefix = L"OffWork-update-";
        if (!name.starts_with(prefix) || name.size() != prefix.size() + 38) continue;
        GUID id;
        if (FAILED(CLSIDFromString(name.c_str() + prefix.size(), &id))) continue;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        std::error_code stampError;
        const auto stamp = std::filesystem::last_write_time(path, stampError);
        // Keep recent/current installers. Delete only our exact setup filename,
        // never recursively remove a directory or follow a directory junction.
        if (stampError || std::filesystem::file_time_type::clock::now() - stamp < std::chrono::hours(24)) continue;
        DeleteFileW((path / L"OffWork-Setup.exe").c_str());
        RemoveDirectoryW(path.c_str());
    }
}

// Synchronous WinHTTP runs exclusively on a worker. All handles are scoped to
// it; no AppState/Updater references cross the thread boundary.
bool Fetch(const std::wstring& url, std::size_t limit, const std::atomic_bool& cancelled,
           const std::function<bool(const char*, DWORD)>& consume,
           const UpdateProgress& progress = {}) {
    if (cancelled) return false;
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring resource(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session(WinHttpOpen(L"OffWork/" OFFWORK_VERSION_W,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 15000)) return false;
    Internet connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) return false;
    Internet request(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) return false;
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy))) return false;
    if (cancelled || !WinHttpSendRequest(request.get(),
        L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || cancelled ||
        !WinHttpReceiveResponse(request.get(), nullptr) || cancelled) return false;
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return false;
    DWORD expected = 0;
    size = sizeof(expected);
    const bool hasLength = WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &expected, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
    if (hasLength && expected > limit) return false;
    const auto contentLength = hasLength ? std::optional<std::size_t>(expected) : std::nullopt;
    int lastPercent = -2;
    ULONGLONG lastProgress = 0;
    const auto report = [&](std::size_t total) {
        if (!progress || cancelled) return;
        const int percent = InstallerDownloadPercent(total, contentLength);
        const auto now = GetTickCount64();
        // At most ten notifications/second, plus the initial notification.
        // Unknown lengths keep an indeterminate arc moving as bytes arrive.
        if (lastPercent != -2 && now - lastProgress < 100) return;
        if (percent == lastPercent && percent != -1) return;
        progress(percent);
        lastPercent = percent;
        lastProgress = now;
    };
    report(0);
    std::size_t total = 0;
    const auto deadline = GetTickCount64() + 5 * 60 * 1000;
    std::array<char, 32768> buffer;
    while (!cancelled && GetTickCount64() < deadline) {
        DWORD count = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count)) return false;
        if (cancelled) return false;
        if (!count) return total > 0 && (!hasLength || total == expected);
        total += count;
        if (total > limit || !consume(buffer.data(), count)) return false;
        report(total);
    }
    return false;
}

std::optional<ReleaseInfo> CheckRelease(const std::atomic_bool& cancelled) {
    CleanupStaleDownloads(cancelled);
    std::string json;
    if (!Fetch(L"https://api.github.com/repos/alertform/OffWork/releases/latest", 2 * 1024 * 1024,
        cancelled, [&](const char* bytes, DWORD count) { json.append(bytes, count); return true; })) return {};
    auto info = ParseReleaseInfo(json);
    return info && IsNewerVersion(info->tag, OFFWORK_VERSION) ? info : std::nullopt;
}

std::unique_ptr<DownloadedInstaller> DownloadRelease(const ReleaseInfo& info,
    const std::atomic_bool& cancelled, const UpdateProgress& progress) {
    if (cancelled) return {};
    GUID id;
    wchar_t guid[40] = {};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, guid, 40)) return {};
    auto result = std::make_unique<DownloadedInstaller>();
    result->directory = (std::filesystem::temp_directory_path() / (std::wstring(L"OffWork-update-") + guid)).wstring();
    if (!CreateDirectoryW(result->directory.c_str(), nullptr)) {
        result->directory.clear(); return {};
    }
    result->path = result->directory + L"\\OffWork-Setup.exe";
    File file(CreateFileW(result->path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) return {};
    std::size_t writtenTotal = 0;
    std::array<char, 2> signature = {};
    const std::wstring url(info.installerUrl.begin(), info.installerUrl.end());
    const bool ok = Fetch(url, kMaxInstallerBytes, cancelled, [&](const char* bytes, DWORD count) {
        for (DWORD i = 0; i < count && writtenTotal + i < signature.size(); ++i) {
            signature[writtenTotal + i] = bytes[i];
        }
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes, count, &written, nullptr) || written != count) return false;
        writtenTotal += count;
        return true;
    }, progress);
    if (!ok || cancelled || signature[0] != 'M' || signature[1] != 'Z' ||
        !FlushFileBuffers(file.get()) || cancelled) return {};
    progress(100);
    return result;
}

bool LaunchInstaller(DownloadedInstaller& installer) {
    std::wstring command = L"\"" + installer.path + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(installer.path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, installer.directory.c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    installer.launched = true;
    return true;
}
}  // namespace

DownloadedInstaller::~DownloadedInstaller() {
    if (!launched) {
        if (!path.empty()) DeleteFileW(path.c_str());
        if (!directory.empty()) RemoveDirectoryW(directory.c_str());
    }
}

Updater::Updater(UpdateWork work) : work_(std::move(work)) {
    if (!work_) {
        work_ = [](const auto& release, const auto& cancelled, const auto& progress) {
            UpdateResult result;
            if (release) result.installer = DownloadRelease(*release, cancelled, progress);
            else result.release = CheckRelease(cancelled);
            return result;
        };
    }
}

Updater::~Updater() { Stop(); }

void Updater::Stop() {
    stopped_ = true;
    window_ = nullptr;
    if (task_) {
        // WM_DESTROY calls this before the HWND can be destroyed or reused.
        std::lock_guard lock(task_->mutex);
        task_->cancelled = true;
        task_->window = nullptr;
    }
    task_.reset();
    installer_.reset();
    downloading_ = false;
}

void Updater::Start(bool download) {
    if (stopped_ || !window_ || task_ || installer_) return;
    try {
        auto task = std::make_shared<UpdateTask>();
        task->window = window_;
        task->operation = ++operation_;
        // Detached deliberately: the worker owns ALL its inputs, handles,
        // mailbox, cancellation and cleanup. Destruction never joins network
        // I/O, and Stop prevents any later posts to the HWND.
        std::thread worker([task, work = work_, release = download ? release_ : std::nullopt] {
            UpdateResult result;
            try {
                if (!task->cancelled) {
                    result = work(release, task->cancelled,
                        [task](int percent) { task->Progress(percent); });
                }
            } catch (...) { /* Network, file and allocation failures stay silent. */ }
            task->Complete(std::move(result));
        });
        worker.detach();
        task_ = std::move(task);
        downloading_ = download;
        percent_ = -1;
        progressPhase_ = 0;
    } catch (...) { downloading_ = false; }
}

void Updater::Check(HWND window) {
    if (stopped_ || !window || GetTickCount64() < nextCheck_ || task_ || installer_) return;
    window_ = window;
    nextCheck_ = GetTickCount64() + kCheckInterval;
    Start(false);
}

void Updater::Download() { if (release_) Start(true); }

void Updater::Complete(WPARAM operation) {
    if (!task_ || operation != operation_) return;
    std::optional<UpdateResult> result;
    {
        std::lock_guard lock(task_->mutex);
        result = std::move(task_->result);
    }
    if (!result) return;
    task_.reset();
    if (result->release) release_ = std::move(result->release);
    installer_ = std::move(result->installer);
    downloading_ = installer_ != nullptr;
    percent_ = downloading_ ? 100 : -1;
}

void Updater::Progress(WPARAM operation, LPARAM percent) {
    if (!task_ || !downloading_ || operation != operation_ || percent < -1 || percent > 100) return;
    percent_ = static_cast<int>(percent);
    ++progressPhase_;
}

bool Updater::LaunchReadyInstaller() {
    if (!installer_) return false;
    const bool launched = LaunchInstaller(*installer_);
    installer_.reset();
    downloading_ = false;
    percent_ = -1;
    return launched;
}

}  // namespace offwork
