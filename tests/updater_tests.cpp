#include "check.h"
#include "updater.h"

#include <chrono>
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace offwork;
using namespace std::chrono_literals;

struct Window {
    HWND handle = CreateWindowExW(0, L"STATIC", L"update test", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    Window() { CHECK(handle != nullptr); }
    ~Window() { if (handle) DestroyWindow(handle); }
};

MSG WaitFor(HWND window, UINT message) {
    const auto deadline = GetTickCount64() + 3000;
    MSG result = {};
    while (!PeekMessageW(&result, window, message, message, PM_REMOVE)) {
        CHECK(GetTickCount64() < deadline);
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
    }
    return result;
}

void WorkersNotifyOnlyThroughMessages() {
    Window window;
    const auto uiThread = std::this_thread::get_id();
    std::promise<void> checkEntered, allowCheck, downloadEntered, allowDownload;
    auto checkGate = allowCheck.get_future().share();
    auto downloadGate = allowDownload.get_future().share();
    std::atomic_int calls = 0;
    Updater updater([&](const auto& release, const auto&, const UpdateProgress& progress) {
        CHECK(std::this_thread::get_id() != uiThread);
        ++calls;
        UpdateResult result;
        if (!release) {
            checkEntered.set_value();
            checkGate.wait();
            result.release = ReleaseInfo{"v9.0.0", "unused"};
        } else {
            CHECK(release->tag == "v9.0.0");
            downloadEntered.set_value();
            progress(37);
            downloadGate.wait();
            progress(75);
        }
        return result;
    });
    updater.Check(window.handle);
    CHECK(checkEntered.get_future().wait_for(2s) == std::future_status::ready);
    // The worker is deliberately blocked: the UI can still process messages.
    CHECK(PostMessageW(window.handle, WM_APP + 22, 123, 0));
    CHECK(WaitFor(window.handle, WM_APP + 22).wParam == 123);
    updater.Check(window.handle);
    CHECK(calls == 1);
    CHECK(!updater.Available());
    allowCheck.set_value();
    const auto checked = WaitFor(window.handle, kUpdateCompleteMessage);
    CHECK(!updater.Available()); // Posting alone never mutates UI state.
    updater.Complete(checked.wParam + 1);
    CHECK(!updater.Available());
    updater.Complete(checked.wParam);
    CHECK(updater.Available());
    updater.Download();
    CHECK(updater.Downloading());
    CHECK(downloadEntered.get_future().wait_for(2s) == std::future_status::ready);
    const auto first = WaitFor(window.handle, kUpdateProgressMessage);
    CHECK(updater.DownloadPercent() == -1);
    updater.Progress(checked.wParam, 99); // Previous operation's notification.
    CHECK(updater.DownloadPercent() == -1);
    updater.Progress(first.wParam, first.lParam);
    CHECK(updater.DownloadPercent() == 37);
    updater.Progress(first.wParam, 101);
    CHECK(updater.DownloadPercent() == 37);
    updater.Download();
    CHECK(calls == 2);
    allowDownload.set_value();
    const auto second = WaitFor(window.handle, kUpdateProgressMessage);
    updater.Progress(second.wParam, second.lParam);
    CHECK(updater.DownloadPercent() == 75);
    const auto downloaded = WaitFor(window.handle, kUpdateCompleteMessage);
    updater.Complete(downloaded.wParam);
    CHECK(!updater.Downloading()); // Failed download restores retry affordance.
    CHECK(updater.Available());
    CHECK(!updater.LaunchReadyInstaller());
    updater.Progress(second.wParam, 90);
    CHECK(updater.DownloadPercent() == -1);
    updater.Stop();
}

struct WorkerLifetime {
    std::promise<void> finished;
    ~WorkerLifetime() { finished.set_value(); }
};

void ClosingCancelsWithoutWaitingOrPosting(bool download, bool destroyWindow) {
    Window window;
    auto lifetime = std::make_shared<WorkerLifetime>();
    auto finished = lifetime->finished.get_future();
    std::promise<void> entered, allowFinish;
    auto gate = allowFinish.get_future().share();
    std::atomic_bool sawCancellation = false;
    auto updater = std::make_unique<Updater>(
        [&, lifetime, gate](const auto& release, const auto& cancelled, const UpdateProgress& progress) {
            UpdateResult result;
            if (download && !release) {
                result.release = ReleaseInfo{"v9.0.0", "unused"};
                return result;
            }
            entered.set_value();
            gate.wait();
            sawCancellation = cancelled.load();
            progress(77); // Even an attempted late notification must be dropped.
            return result;
        });
    lifetime.reset(); // Only the updater/worker now own the lifetime sentinel.
    updater->Check(window.handle);
    if (download) {
        const auto checked = WaitFor(window.handle, kUpdateCompleteMessage);
        updater->Complete(checked.wParam);
        updater->Download();
    }
    CHECK(entered.get_future().wait_for(2s) == std::future_status::ready);
    const auto start = std::chrono::steady_clock::now();
    updater->Stop();
    updater->Stop(); // Idempotent, including the destructor's subsequent call.
    updater->Check(window.handle);
    updater->Download();
    updater.reset();
    CHECK(std::chrono::steady_clock::now() - start < 250ms);
    if (destroyWindow) {
        CHECK(DestroyWindow(window.handle));
        window.handle = nullptr;
    }
    Window replacement;
    allowFinish.set_value();
    CHECK(finished.wait_for(2s) == std::future_status::ready);
    CHECK(sawCancellation);
    MSG message = {};
    // Check the thread's whole queue, including any reused/replacement HWND.
    CHECK(!PeekMessageW(&message, nullptr, kUpdateCompleteMessage,
                       kUpdateProgressMessage, PM_REMOVE));
}

void ExceptionsStillNotifyCompletion() {
    Window window;
    Updater updater([](const auto&, const auto&, const auto&) -> UpdateResult {
        throw std::runtime_error("network failure");
    });
    updater.Check(window.handle);
    const auto failed = WaitFor(window.handle, kUpdateCompleteMessage);
    updater.Complete(failed.wParam);
    CHECK(!updater.Available() && !updater.Downloading());
}

void CompletedInstallerIsCleanedOnClose() {
    Window window;
    const auto directory = std::filesystem::temp_directory_path() /
        (L"offwork-updater-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = directory / L"OffWork-Setup.exe";
    std::atomic_int calls = 0;
    Updater updater([&](const auto& release, const auto&, const auto&) {
        ++calls;
        UpdateResult result;
        if (!release) result.release = ReleaseInfo{"v9.0.0", "unused"};
        else {
            CHECK(std::filesystem::create_directory(directory));
            std::ofstream file(path, std::ios::binary);
            file << "test data only - never executed";
            CHECK(file.good());
            result.installer = std::make_unique<DownloadedInstaller>();
            result.installer->directory = directory.wstring();
            result.installer->path = path.wstring();
        }
        return result;
    });
    updater.Check(window.handle);
    updater.Complete(WaitFor(window.handle, kUpdateCompleteMessage).wParam);
    updater.Download();
    updater.Complete(WaitFor(window.handle, kUpdateCompleteMessage).wParam);
    CHECK(updater.Downloading() && updater.DownloadPercent() == 100);
    CHECK(std::filesystem::exists(path));
    updater.Download();
    CHECK(calls == 2); // A ready installer awaiting OCR cannot be overwritten.
    updater.Stop();
    CHECK(!std::filesystem::exists(path) && !std::filesystem::exists(directory));
}
} // namespace

int main() {
    WorkersNotifyOnlyThroughMessages();
    for (bool download : {false, true}) {
        for (bool destroyWindow : {false, true}) {
            ClosingCancelsWithoutWaitingOrPosting(download, destroyWindow);
        }
    }
    ExceptionsStillNotifyCompletion();
    CompletedInstallerIsCleanedOnClose();
    std::cout << "Updater worker, message, cancellation and lifetime tests passed.\n";
}
